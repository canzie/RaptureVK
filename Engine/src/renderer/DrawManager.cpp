#include "renderer/DrawManager.h"

#include "app/Application.h"
#include "core/jobs/Counter.h"
#include "core/jobs/Job.h"
#include "core/jobs/JobCommon.h"
#include "core/jobs/JobSystem.h"
#include "core/utils/Log.h"
#include "core/utils/TracyProfiler.h"
#include "core/utils/rp_assert.h"
#include "renderer/Renderer.h"
#include "renderer/passes/RenderPassContext.h"
#include "scene/Scene.h"
#include "scene/components/Components.h"
#include "scene/components/TerrainComponent.h"

#include <algorithm>

namespace Rapture {

DrawManager::DrawManager(RenderContext renderContext, const DrawManagerConfig &config)
    : m_renderContext(renderContext), m_config(config)
{
    RP_ASSERT(m_config.framesInFlight > 0, "a draw manager needs at least one frame in flight");

    auto &vc = *m_renderContext.vulkanContext;

    m_swapChain = Application::getInstance().getMainWindow().getSwapChain();
    m_graphicsQueue = vc.getGraphicsQueue();
    m_presentQueue = vc.getPresentQueue();

    setupCommandResources();
    createRenderTarget();
    createSharedTextures();

    m_opaqueGeometry = std::make_unique<SceneGeometryDraw>(m_renderContext, m_config.framesInFlight);

    m_swapChainRecreatedConn = m_swapChain->onRecreated.connect([this]() { onSwapChainRecreated(); });
}

DrawManager::~DrawManager()
{
    m_renderContext.vulkanContext->waitIdle();

    for (std::vector<RendererSlot> &slots : m_renderers) {
        slots.clear();
    }

    m_opaqueGeometry.reset();
    m_depthTextures.clear();
    m_sceneRenderTarget.reset();

    m_graphicsQueue.reset();
    m_presentQueue.reset();
    m_swapChain.reset();
}

void DrawManager::setupCommandResources()
{
    CommandPoolConfig config = {};
    config.queueFamilyIndex = m_renderContext.vulkanContext->getGraphicsQueueIndex();
    config.flags = 0;

    m_commandPoolHash = m_renderContext.commandPoolManager->createCommandPool(config);
}

void DrawManager::createRenderTarget()
{
    if (m_config.targetType == SceneRenderTarget::TargetType::OFFSCREEN) {
        m_sceneRenderTarget = std::make_unique<SceneRenderTarget>(m_config.width, m_config.height, m_config.framesInFlight,
                                                                  TextureFormat::RGBA16F, m_config.allowReadback);
    } else {
        m_sceneRenderTarget = std::make_unique<SceneRenderTarget>(m_swapChain);
        m_config.width = m_swapChain->getExtent().width;
        m_config.height = m_swapChain->getExtent().height;
    }
}

void DrawManager::createSharedTextures()
{
    m_depthTextures.clear();
    m_sceneColorHdrTextures.clear();

    TextureSpecification depthSpec;
    depthSpec.width = m_config.width;
    depthSpec.height = m_config.height;
    depthSpec.format = TextureFormat::D24S8;
    depthSpec.type = TextureType::TEXTURE2D;
    depthSpec.srgb = false;

    TextureSpecification colorSpec;
    colorSpec.width = m_config.width;
    colorSpec.height = m_config.height;
    colorSpec.format = TextureFormat::RGBA16F;
    colorSpec.type = TextureType::TEXTURE2D;
    colorSpec.srgb = false;

    for (uint32_t frame = 0; frame < m_config.framesInFlight; frame++) {
        m_depthTextures.push_back(std::make_unique<Texture>(depthSpec));
        m_sceneColorHdrTextures.push_back(std::make_unique<Texture>(colorSpec));
    }

    m_compositePass = std::make_unique<CompositePass>(static_cast<float>(m_config.width), static_cast<float>(m_config.height),
                                                      getOutputFormat());
}

VkFormat DrawManager::getDepthFormat() const
{
    if (m_depthTextures.empty()) {
        return VK_FORMAT_UNDEFINED;
    }

    return m_depthTextures[0]->getFormat();
}

VkFormat DrawManager::getSceneColorFormat() const
{
    if (m_sceneColorHdrTextures.empty()) {
        return VK_FORMAT_UNDEFINED;
    }

    return m_sceneColorHdrTextures[0]->getFormat();
}

void DrawManager::populateOpaqueGeometry(Scene &scene, ecs::EntityAccessor camera)
{
    const Frustum *frustum = nullptr;

    if (camera.isValid() && scene.getSettings().frustumCullingEnabled) {
        const CameraComponent *cameraComp = camera.tryRead<CameraComponent>();
        if (cameraComp != nullptr) {
            frustum = &cameraComp->frustum;
        }
    }

    m_opaqueGeometry->populate(scene, frustum, m_currentFrame);
}

void DrawManager::addRenderer(std::unique_ptr<Renderer> renderer, DrawPhase phase, uint32_t order)
{
    RP_ASSERT(renderer != nullptr, "a draw manager cannot run a null renderer");

    std::vector<RendererSlot> &slots = m_renderers[phase];
    auto slot = std::lower_bound(slots.begin(), slots.end(), order,
                                 [](const RendererSlot &candidate, uint32_t position) { return candidate.order < position; });

    RP_ASSERT(slot == slots.end() || slot->order != order, "order {} of this phase is already taken", order);

    slots.insert(slot, RendererSlot{order, std::move(renderer)});
}

void DrawManager::setRenderer(std::unique_ptr<Renderer> renderer, DrawPhase phase, uint32_t order)
{
    RP_ASSERT(renderer != nullptr, "a draw manager cannot run a null renderer");

    m_renderContext.vulkanContext->waitIdle();

    std::vector<RendererSlot> &slots = m_renderers[phase];
    auto slot = std::lower_bound(slots.begin(), slots.end(), order,
                                 [](const RendererSlot &candidate, uint32_t position) { return candidate.order < position; });

    if (slot != slots.end() && slot->order == order) {
        slot->renderer = std::move(renderer);
        return;
    }

    slots.insert(slot, RendererSlot{order, std::move(renderer)});
}

void DrawManager::recordPhase(DrawPhase phase, const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    std::vector<RendererSlot> &slots = m_renderers[phase];
    if (slots.empty()) {
        return;
    }

    JobSystem &system = jobs();

    Counter recordCounter{};
    recordCounter.increment(static_cast<uint32_t>(slots.size()));

    for (RendererSlot &slot : slots) {
        system.run(JobDeclaration(
            [renderer = slot.renderer.get(), &context](JobContext &ctx) { renderer->recordSecondaries(context, ctx); },
            JobPriority::HIGH, QueueAffinity::ANY, &recordCounter, slot.renderer->name()));
    }

    {
        RAPTURE_PROFILE_SCOPE("Renderer Record Wait");
        system.waitFor(recordCounter, 0);
    }

    for (RendererSlot &slot : slots) {
        slot.renderer->replay(context, commandBuffer);
    }
}

void DrawManager::recordComposite(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Composite Pass");

    CommandBuffer *secondary = m_compositePass->record(context, m_compositePass->getInheritance(context));
    if (secondary == nullptr) {
        return;
    }

    m_compositePass->beginRendering(context, commandBuffer);
    commandBuffer->executeSecondary(*secondary);
    m_compositePass->endRendering(commandBuffer);
}

TerrainGenerator *DrawManager::findTerrain(Scene &scene)
{
    for (auto [entity, terrainComp] : scene.getRegistry().read<TerrainComponent>()) {
        if (terrainComp.generator && terrainComp.isEnabled && terrainComp.generator->isInitialized()) {
            return terrainComp.generator.get();
        }
        break;
    }

    return nullptr;
}

bool DrawManager::hasRenderers() const
{
    for (const std::vector<RendererSlot> &slots : m_renderers) {
        if (!slots.empty()) {
            return true;
        }
    }

    return false;
}

void DrawManager::resize(uint32_t width, uint32_t height)
{
    // the swapchain resizes itself and reports it through onSwapChainRecreated
    if (m_config.targetType == SceneRenderTarget::TargetType::SWAPCHAIN) {
        m_swapChainNeedsResize = true;
        return;
    }

    m_pendingWidth = width;
    m_pendingHeight = height;
    m_resizePending = true;
}

void DrawManager::applyPendingResize()
{
    m_resizePending = false;

    if (m_config.width == m_pendingWidth && m_config.height == m_pendingHeight) {
        return;
    }

    // waiting is not enough on its own: a frame that ended in addToBatch is recorded but not yet
    // submitted, and still references what is about to be destroyed. Held until the top of a frame,
    // which is past the point where the last one was flushed.
    m_renderContext.vulkanContext->waitIdle();

    m_currentFrame = 0;
    m_config.width = m_pendingWidth;
    m_config.height = m_pendingHeight;

    m_sceneRenderTarget->resize(m_config.width, m_config.height);
    createSharedTextures();

    for (std::vector<RendererSlot> &slots : m_renderers) {
        for (RendererSlot &slot : slots) {
            slot.renderer->onResize(m_config.width, m_config.height);
        }
    }

    RP_CORE_INFO("Resized render target to {}x{}", m_config.width, m_config.height);
}

void DrawManager::onSwapChainRecreated()
{
    m_renderContext.vulkanContext->waitIdle();

    if (m_config.targetType == SceneRenderTarget::TargetType::SWAPCHAIN) {
        m_config.width = m_swapChain->getExtent().width;
        m_config.height = m_swapChain->getExtent().height;

        m_sceneRenderTarget = std::make_unique<SceneRenderTarget>(m_swapChain);
        createSharedTextures();

        for (std::vector<RendererSlot> &slots : m_renderers) {
            for (RendererSlot &slot : slots) {
                slot.renderer->onResize(m_config.width, m_config.height);
            }
        }
    } else if (m_sceneRenderTarget) {
        // an offscreen target keeps its size, only the swapchain reference it holds goes stale
        m_sceneRenderTarget->onSwapChainRecreated();
    }

    m_currentFrame = 0;
    m_swapChainNeedsResize = false;
}

bool DrawManager::acquireImage(uint32_t &imageIndex)
{
    if (m_config.targetType != SceneRenderTarget::TargetType::SWAPCHAIN) {
        imageIndex = m_currentFrame;
        return true;
    }

    int acquired = m_swapChain->acquireImage(m_currentFrame);
    if (acquired == -1) {
        return false;
    }

    imageIndex = static_cast<uint32_t>(acquired);
    return true;
}

void DrawManager::drawFrame(Scene &scene, ecs::EntityAccessor camera, const RenderSettings &settings)
{
    RAPTURE_PROFILE_FUNCTION();

    if (!hasRenderers()) {
        return;
    }

    if (m_resizePending) {
        applyPendingResize();
    }

    m_currentFrame = static_cast<uint32_t>(Application::getInstance().getMonotonicFrameCount() % m_config.framesInFlight);

    uint32_t imageIndex = m_currentFrame;
    if (!acquireImage(imageIndex)) {
        return;
    }

    auto pool = m_renderContext.commandPoolManager->getCommandPool(m_commandPoolHash, m_currentFrame);
    auto commandBuffer = pool->getPrimaryCommandBuffer();

    recordFrame(commandBuffer, scene, camera, imageIndex, settings);

    if (!submitFrame(commandBuffer, imageIndex)) {
        return;
    }

    m_lastRenderedFrame = imageIndex;
}

void DrawManager::recordFrame(CommandBuffer *commandBuffer, Scene &scene, ecs::EntityAccessor camera, uint32_t imageIndex,
                              const RenderSettings &settings)
{
    RAPTURE_PROFILE_FUNCTION();

    if (commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to begin recording command buffer!");
        return;
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "DrawManager Frame");

        m_sharedTargets.depthStencil = m_depthTextures[m_currentFrame].get();
        m_sharedTargets.sceneColorHdr = m_sceneColorHdrTextures[m_currentFrame].get();
        populateOpaqueGeometry(scene, camera);

        RenderPassContext context;
        context.scene = &scene;
        context.camera = camera;
        context.renderTarget = m_sceneRenderTarget.get();
        context.targets = &m_sharedTargets;
        context.settings = &settings;
        context.terrain = findTerrain(scene);
        context.opaqueGeometry = m_opaqueGeometry.get();
        context.frameInFlight = m_currentFrame;
        context.imageIndex = imageIndex;

        recordPhase(DRAW_PHASE_PRE_COMPOSITE, context, commandBuffer);
        recordComposite(context, commandBuffer);
        recordPhase(DRAW_PHASE_POST_COMPOSITE, context, commandBuffer);

        if (m_sceneRenderTarget->requiresSamplingTransition()) {
            m_sceneRenderTarget->transitionToShaderReadLayout(commandBuffer, imageIndex);
        }

        RAPTURE_PROFILE_GPU_COLLECT(commandBuffer->getCommandBufferVk());
    }

    if (commandBuffer->end() != VK_SUCCESS) {
        RP_CORE_ERROR("failed to record command buffer!");
    }
}

bool DrawManager::submitFrame(CommandBuffer *commandBuffer, uint32_t imageIndex)
{
    if (m_config.targetType != SceneRenderTarget::TargetType::SWAPCHAIN) {
        m_graphicsQueue->addToBatch(commandBuffer);
        return true;
    }

    VkSemaphore waitSemaphore = m_swapChain->getImageAvailableSemaphore(m_currentFrame);
    VkSemaphore signalSemaphore = m_swapChain->getRenderFinishedSemaphore(m_currentFrame);
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    std::span<VkSemaphore> waitSemaphores(&waitSemaphore, 1);
    std::span<VkSemaphore> signalSemaphores(&signalSemaphore, 1);

    m_graphicsQueue->submitAndFlushQueue(commandBuffer, &signalSemaphores, &waitSemaphores, &waitStage,
                                         m_swapChain->getInFlightFence(m_currentFrame));

    VkSwapchainKHR swapChain = m_swapChain->getSwapChainVk();

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &signalSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr;

    VkResult result = m_presentQueue->presentQueue(presentInfo);
    m_swapChain->signalImageAvailability(imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_swapChainNeedsResize) {
        m_swapChain->onRecreationRequested.fire();
        return false;
    }

    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("failed to present swap chain image!");
        return false;
    }

    return true;
}

} // namespace Rapture

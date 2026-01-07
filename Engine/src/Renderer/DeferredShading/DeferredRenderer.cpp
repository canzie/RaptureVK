#include "DeferredRenderer.h"

#include "Buffers/CommandBuffers/CommandPool.h"
#include "WindowContext/Application.h"

#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"

#include "Components/TerrainComponent.h"
#include "Events/ApplicationEvents.h"
#include "Renderer/Shadows/ShadowCommon.h"
#include "jobs/InplaceFunction.h"
#include "jobs/Job.h"
#include "jobs/JobCommon.h"
#include "jobs/JobSystem.h"
#include <cstdio>

namespace Rapture {

// Maximum number of lights supported
static constexpr uint32_t MAX_LIGHTS = 16;

// Static member definitions
CommandPoolHash DeferredRenderer::m_commandPoolHash = 0;
VmaAllocator DeferredRenderer::m_vmaAllocator = VK_NULL_HANDLE;
VkDevice DeferredRenderer::m_device = VK_NULL_HANDLE;
std::shared_ptr<SwapChain> DeferredRenderer::m_swapChain = nullptr;
std::shared_ptr<SceneRenderTarget> DeferredRenderer::m_sceneRenderTarget = nullptr;
uint32_t DeferredRenderer::m_currentFrame = 0;
std::shared_ptr<VulkanQueue> DeferredRenderer::m_graphicsQueue = nullptr;
std::shared_ptr<VulkanQueue> DeferredRenderer::m_presentQueue = nullptr;
// PASSES
std::shared_ptr<GBufferPass> DeferredRenderer::m_gbufferPass = nullptr;
std::shared_ptr<LightingPass> DeferredRenderer::m_lightingPass = nullptr;
std::shared_ptr<StencilBorderPass> DeferredRenderer::m_stencilBorderPass = nullptr;
std::shared_ptr<SkyboxPass> DeferredRenderer::m_skyboxPass = nullptr;
std::shared_ptr<InstancedShapesPass> DeferredRenderer::m_instancedShapesPass = nullptr;

float DeferredRenderer::m_width = 0.0f;
float DeferredRenderer::m_height = 0.0f;
bool DeferredRenderer::m_framebufferNeedsResize = false;
uint32_t DeferredRenderer::m_pendingViewportWidth = 0;
uint32_t DeferredRenderer::m_pendingViewportHeight = 0;
bool DeferredRenderer::m_viewportResizePending = false;
std::shared_ptr<DynamicDiffuseGI> DeferredRenderer::m_dynamicDiffuseGI = nullptr;
std::shared_ptr<RtInstanceData> DeferredRenderer::m_rtInstanceData = nullptr;

static Counter s_cmdCounter{};

void DeferredRenderer::init()
{

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    m_device = vc.getLogicalDevice();
    m_swapChain = vc.getSwapChain();
    m_vmaAllocator = vc.getVmaAllocator();

    m_graphicsQueue = vc.getGraphicsQueue();
    m_presentQueue = vc.getPresentQueue();

    m_width = static_cast<float>(m_swapChain->getExtent().width);
    m_height = static_cast<float>(m_swapChain->getExtent().height);

    setupCommandResources();
    createRenderTarget();

    m_rtInstanceData = std::make_shared<RtInstanceData>();
    m_dynamicDiffuseGI = std::make_shared<DynamicDiffuseGI>(m_swapChain->getImageCount());

    // Get the render target format for pipeline creation
    VkFormat colorFormat = m_sceneRenderTarget->getFormat();

    m_gbufferPass = std::make_shared<GBufferPass>(m_width, m_height, m_swapChain->getImageCount());

    m_lightingPass = std::make_shared<LightingPass>(m_width, m_height, m_gbufferPass, m_dynamicDiffuseGI, colorFormat);

    m_stencilBorderPass = std::make_shared<StencilBorderPass>(m_width, m_height, m_swapChain->getImageCount(),
                                                              m_gbufferPass->getDepthTextures(), colorFormat);

    m_instancedShapesPass = std::make_shared<InstancedShapesPass>(m_width, m_height, m_swapChain->getImageCount(),
                                                                  m_gbufferPass->getDepthTextures(), colorFormat);

    m_skyboxPass = std::make_shared<SkyboxPass>(m_gbufferPass->getDepthTextures(), colorFormat);

    ApplicationEvents::onWindowResize().addListener([](unsigned int width, unsigned int height) {
        (void)width;
        (void)height;
        m_framebufferNeedsResize = true;
    });

    ApplicationEvents::onSwapChainRecreated().addListener([](std::shared_ptr<SwapChain> swapChain) {
        (void)swapChain;
        onSwapChainRecreated();
    });

    // Listen for viewport resize events (Editor mode only)
    // We defer the actual resize to the start of the next frame to avoid
    // mid-frame resource destruction issues
    if (SwapChain::renderMode == RenderMode::OFFSCREEN) {
        ApplicationEvents::onViewportResize().addListener([](unsigned int width, unsigned int height) {
            m_pendingViewportWidth = width;
            m_pendingViewportHeight = height;
            m_viewportResizePending = true;
        });
    }
}

void DeferredRenderer::shutdown()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    vc.waitIdle();

    m_skyboxPass.reset();
    m_stencilBorderPass.reset();
    m_lightingPass.reset();
    m_gbufferPass.reset();
    m_instancedShapesPass.reset();

    // Clean up scene render target
    m_sceneRenderTarget.reset();

    // Clean up queues
    m_graphicsQueue.reset();
    m_presentQueue.reset();

    // Clean up swapchain
    m_swapChain.reset();
}

void DeferredRenderer::drawFrame(std::shared_ptr<Scene> activeScene)
{

    RAPTURE_PROFILE_FUNCTION();

    if (m_viewportResizePending) {
        processPendingViewportResize();
    }

    // For PRESENTATION mode, we need to acquire a swapchain image
    // For OFFSCREEN mode, we just use m_currentFrame as the target index
    uint32_t imageIndex = m_currentFrame;

    if (SwapChain::renderMode == RenderMode::PRESENTATION) {
        int imageIndexi = m_swapChain->acquireImage(m_currentFrame);
        if (imageIndexi == -1) {
            return;
        }
        imageIndex = static_cast<uint32_t>(imageIndexi);
    }

    m_rtInstanceData->update(activeScene);

    JobSystem &system = jobs();
    system.run(JobDeclaration(
        [m_dynamicDiffuseGI = m_dynamicDiffuseGI, activeScene, m_currentFrame = m_currentFrame](JobContext &ctx) {
            (void)ctx;
            m_dynamicDiffuseGI->populateProbesCompute(activeScene, m_currentFrame);
        },
        JobPriority::NORMAL, QueueAffinity::COMPUTE, nullptr, "DDGI POPULATE"));

    // m_dynamicDiffuseGI->populateProbesCompute(activeScene, m_currentFrame);

    auto pool = CommandPoolManager::getCommandPool(m_commandPoolHash, m_currentFrame);
    auto commandBuffer = pool->getPrimaryCommandBuffer();

    recordCommandBuffer(commandBuffer, activeScene, imageIndex);

    VkSemaphore frWaitSemaphores[1];
    VkSemaphore frSignalSemaphores[1];
    VkPipelineStageFlags frWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    if (SwapChain::renderMode == RenderMode::PRESENTATION) {
        // PRESENTATION mode: Wait for swapchain image, signal when done, then present
        frWaitSemaphores[0] = m_swapChain->getImageAvailableSemaphore(m_currentFrame);
        frSignalSemaphores[0] = m_swapChain->getRenderFinishedSemaphore(m_currentFrame);

        std::span<VkSemaphore> waitSemaphores(frWaitSemaphores, 1);
        std::span<VkSemaphore> signalSemaphores(frSignalSemaphores, 1);

        m_graphicsQueue->submitAndFlushQueue(commandBuffer, &signalSemaphores, &waitSemaphores, &frWaitStage,
                                             m_swapChain->getInFlightFence(m_currentFrame));

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        // Presentation must wait for rendering to be complete.
        // frSignalSemaphores contains the renderFinishedSemaphore for PRESENTATION
        // mode.
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = frSignalSemaphores;

        VkSwapchainKHR swapChains[] = {m_swapChain->getSwapChainVk()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;
        presentInfo.pResults = nullptr;

        VkResult result = m_presentQueue->presentQueue(presentInfo);
        m_swapChain->signalImageAvailability(imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferNeedsResize) {
            ApplicationEvents::onRequestSwapChainRecreation().publish();
            return;
        } else if (result != VK_SUCCESS) {
            RP_CORE_ERROR("failed to present swap chain image!");
            return;
        }
    } else {
        m_graphicsQueue->addToBatch(commandBuffer);

        // OFFSCREEN mode: Do NOT submit here - ImguiLayer handles submission
        // The command buffer has been recorded and added to the queue.
        // ImguiLayer will submit it with proper semaphore synchronization.
    }

    m_currentFrame = (m_currentFrame + 1) % m_swapChain->getImageCount();
}

void DeferredRenderer::onSwapChainRecreated()
{
    // Wait for all operations to complete
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    vc.waitIdle();
    jobs().waitFor(s_cmdCounter, 0);

    // In PRESENTATION mode, the render target is backed by swapchain, so we need to recreate everything
    // In OFFSCREEN mode, only update the swapchain reference (image count may have changed)
    if (SwapChain::renderMode == RenderMode::PRESENTATION) {
        m_width = static_cast<float>(m_swapChain->getExtent().width);
        m_height = static_cast<float>(m_swapChain->getExtent().height);

        // Recreate the swapchain-backed render target
        m_sceneRenderTarget = std::make_shared<SceneRenderTarget>(m_swapChain);

        // Recreate all render passes with new dimensions
        recreateRenderPasses();
    } else {
        // OFFSCREEN mode: swapchain recreation doesn't affect our render target size
        // Just update the reference
        if (m_sceneRenderTarget) {
            m_sceneRenderTarget->onSwapChainRecreated();
        }
    }

    m_currentFrame = 0;
    m_framebufferNeedsResize = false;
}

void DeferredRenderer::createRenderTarget()
{
    if (SwapChain::renderMode == RenderMode::OFFSCREEN) {
        // Create offscreen render target for Editor mode
        // Use BGRA8 SRGB format (matches typical swapchain format)
        m_sceneRenderTarget = std::make_shared<SceneRenderTarget>(static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height),
                                                                  m_swapChain->getImageCount(), TextureFormat::RGBA16F);
        RP_CORE_INFO("Created OFFSCREEN render target for Editor mode");
    } else {
        // Create swapchain-backed render target for Standalone mode
        m_sceneRenderTarget = std::make_shared<SceneRenderTarget>(m_swapChain);
        RP_CORE_INFO("Created SWAPCHAIN-backed render target for Standalone mode");
    }
}

void DeferredRenderer::recreateRenderPasses()
{
    jobs().waitFor(s_cmdCounter, 0);

    m_skyboxPass.reset();
    m_stencilBorderPass.reset();
    m_lightingPass.reset();
    m_gbufferPass.reset();
    m_instancedShapesPass.reset();

    VkFormat colorFormat = m_sceneRenderTarget->getFormat();

    m_gbufferPass = std::make_shared<GBufferPass>(m_width, m_height, m_swapChain->getImageCount());

    m_lightingPass = std::make_shared<LightingPass>(m_width, m_height, m_gbufferPass, m_dynamicDiffuseGI, colorFormat);

    m_stencilBorderPass = std::make_shared<StencilBorderPass>(m_width, m_height, m_swapChain->getImageCount(),
                                                              m_gbufferPass->getDepthTextures(), colorFormat);

    m_instancedShapesPass = std::make_shared<InstancedShapesPass>(m_width, m_height, m_swapChain->getImageCount(),
                                                                  m_gbufferPass->getDepthTextures(), colorFormat);

    m_skyboxPass = std::make_shared<SkyboxPass>(m_gbufferPass->getDepthTextures(), colorFormat);
}

void DeferredRenderer::processPendingViewportResize()
{
    uint32_t width = m_pendingViewportWidth;
    uint32_t height = m_pendingViewportHeight;
    m_viewportResizePending = false;

    if (static_cast<uint32_t>(m_width) == width && static_cast<uint32_t>(m_height) == height) {
        return; // No change
    }

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    vc.waitIdle();

    m_currentFrame = 0;
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);

    // Resize the offscreen render target
    m_sceneRenderTarget->resize(width, height);

    // Recreate render passes with new dimensions
    recreateRenderPasses();

    RP_CORE_INFO("Resized render target to {}x{}", width, height);
}

void DeferredRenderer::setupCommandResources()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    CommandPoolConfig config = {};
    config.queueFamilyIndex = vc.getGraphicsQueueIndex();
    config.flags = 0;
    m_commandPoolHash = CommandPoolManager::createCommandPool(config);
}

void DeferredRenderer::recordCommandBuffer(CommandBuffer *commandBuffer, std::shared_ptr<Scene> activeScene, uint32_t imageIndex)
{

    RAPTURE_PROFILE_FUNCTION();

    // Query for SkyboxComponent - could be on any entity (typically environment entity)
    if (!m_skyboxPass->hasActiveSkybox()) {
        auto view = activeScene->getRegistry().view<SkyboxComponent>();
        if (!view.empty()) {
            auto &skyboxComp = view.get<SkyboxComponent>(*view.begin());
            m_skyboxPass->setSkyboxTexture(skyboxComp.skyboxTexture);
        }
    }

    if (commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to begin recording command buffer!");
        return;
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "DeferredRenderer Frame");

        auto &registry = activeScene->getRegistry();
        auto lightView = registry.view<LightComponent, TransformComponent, ShadowComponent>();
        auto cascadedShadowView = registry.view<LightComponent, TransformComponent, CascadedShadowComponent>();

        TerrainGenerator *terrain = nullptr;
        auto terrainView = registry.view<TerrainComponent>();
        if (!terrainView.empty()) {
            auto &terrainComp = terrainView.get<TerrainComponent>(*terrainView.begin());
            if (terrainComp.generator && terrainComp.isEnabled && terrainComp.generator->isInitialized()) {
                terrain = terrainComp.generator.get();
            }
        }

        {
            RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Shadow Maps");

            for (auto entity : lightView) {
                auto &lightComp = lightView.get<LightComponent>(entity);
                auto &transformComp = lightView.get<TransformComponent>(entity);
                auto &shadowComp = lightView.get<ShadowComponent>(entity);

                bool shouldUpdateShadow = (lightComp.hasChanged(m_currentFrame) || transformComp.hasChanged() ||
                                           lightComp.type == LightType::Directional || lightComp.type == LightType::Spot);

                if (shadowComp.shadowMap && shouldUpdateShadow) {
                    auto shadowBuffer = shadowComp.shadowMap->recordSecondary(activeScene, m_currentFrame);
                    if (shadowBuffer) {
                        shadowComp.shadowMap->beginDynamicRendering(commandBuffer);
                        commandBuffer->executeSecondary(*shadowBuffer);
                        shadowComp.shadowMap->endDynamicRendering(commandBuffer);
                    }
                }
            }

            for (auto entity : cascadedShadowView) {
                auto &lightComp = cascadedShadowView.get<LightComponent>(entity);
                auto &transformComp = cascadedShadowView.get<TransformComponent>(entity);
                auto &shadowComp = cascadedShadowView.get<CascadedShadowComponent>(entity);

                bool shouldUpdateShadow = (lightComp.hasChanged(m_currentFrame) || transformComp.hasChanged() ||
                                           lightComp.type == LightType::Directional);

                if (shadowComp.cascadedShadowMap && shouldUpdateShadow) {
                    auto shadowBuffer = shadowComp.cascadedShadowMap->recordSecondary(activeScene, m_currentFrame, terrain);
                    if (shadowBuffer) {
                        shadowComp.cascadedShadowMap->beginDynamicRendering(commandBuffer);
                        commandBuffer->executeSecondary(*shadowBuffer);
                        shadowComp.cascadedShadowMap->endDynamicRendering(commandBuffer);
                    }
                }
            }
        }

        CommandBuffer *gbufferBuffer = nullptr;
        CommandBuffer *lightingBuffer = nullptr;
        CommandBuffer *skyboxBuffer = nullptr;
        CommandBuffer *instancedShapesBuffer = nullptr;
        CommandBuffer *stencilBorderBuffer = nullptr;

        // mainCounter = Counter();
        s_cmdCounter.increment(4); // GBuffer, Lighting, Skybox, Instanced Shapes

        JobSystem &system = jobs();

        // Prepare GBuffer Pass data

        SecondaryBufferInheritance gbufferInheritance;
        auto fbSpec = GBufferPass::getFramebufferSpecification();
        gbufferInheritance.colorFormats = fbSpec.colorAttachments;
        gbufferInheritance.depthFormat = fbSpec.depthAttachment;
        gbufferInheritance.stencilFormat = fbSpec.stencilAttachment;

        system.run(JobDeclaration(
            [&gbufferBuffer, activeScene, m_currentFrame = m_currentFrame, gbufferInheritance, terrain,
             m_gbufferPass = m_gbufferPass](JobContext &ctx) {
                (void)ctx;
                gbufferBuffer = m_gbufferPass->recordSecondary(activeScene, m_currentFrame, gbufferInheritance, terrain);
            },
            JobPriority::HIGH, QueueAffinity::ANY, &s_cmdCounter, "GBUFFER"));

        SecondaryBufferInheritance lightingInheritance;
        lightingInheritance.colorFormats = {m_sceneRenderTarget->getFormat()};

        system.run(JobDeclaration(
            [&lightingBuffer, activeScene, m_sceneRenderTarget = m_sceneRenderTarget, lightingInheritance,
             m_lightingPass = m_lightingPass](JobContext &ctx) {
                (void)ctx;
                lightingBuffer = m_lightingPass->recordSecondary(activeScene, *m_sceneRenderTarget, lightingInheritance);
            },
            JobPriority::HIGH, QueueAffinity::ANY, &s_cmdCounter, "LIGHTING"));

        SecondaryBufferInheritance skyboxInheritance;
        skyboxInheritance.colorFormats = {m_sceneRenderTarget->getFormat()};
        skyboxInheritance.depthFormat = m_gbufferPass->getDepthTexture()->getFormat();

        system.run(JobDeclaration(
            [&skyboxBuffer, m_sceneRenderTarget = m_sceneRenderTarget, m_currentFrame = m_currentFrame, skyboxInheritance,
             m_skyboxPass = m_skyboxPass](JobContext &ctx) {
                (void)ctx;
                skyboxBuffer = m_skyboxPass->recordSecondary(*m_sceneRenderTarget, m_currentFrame, skyboxInheritance);
            },
            JobPriority::HIGH, QueueAffinity::ANY, &s_cmdCounter, "SKYBOX"));

        SecondaryBufferInheritance instancedInheritance;
        instancedInheritance.colorFormats = {m_sceneRenderTarget->getFormat()};
        instancedInheritance.depthFormat = m_gbufferPass->getDepthTexture()->getFormat();

        system.run(JobDeclaration(
            [&instancedShapesBuffer, activeScene, m_sceneRenderTarget = m_sceneRenderTarget, m_currentFrame = m_currentFrame,
             instancedInheritance, m_instancedShapesPass = m_instancedShapesPass](JobContext &ctx) {
                (void)ctx;
                instancedShapesBuffer =
                    m_instancedShapesPass->recordSecondary(activeScene, *m_sceneRenderTarget, m_currentFrame, instancedInheritance);
            },
            JobPriority::HIGH, QueueAffinity::ANY, &s_cmdCounter, "INSTANCED_SHAPES"));

        {
            SecondaryBufferInheritance stencilInheritance;
            stencilInheritance.colorFormats = {m_sceneRenderTarget->getFormat()};
            // stencilInheritance.depthFormat = m_gbufferPass->getDepthTexture()->getFormat();
            stencilInheritance.stencilFormat = m_gbufferPass->getDepthTexture()->getFormat();

            // stencilBorderBuffer =
            //     m_stencilBorderPass->recordSecondary(*m_sceneRenderTarget, m_currentFrame, activeScene, stencilInheritance);
        }

        {

            RAPTURE_PROFILE_SCOPE("command buffer Wait");
            system.waitFor(s_cmdCounter, 0);
        }
        // Here we wait for all of them to be finished (if in parallel)

        if (gbufferBuffer) {
            m_gbufferPass->beginDynamicRendering(commandBuffer, m_currentFrame);
            commandBuffer->executeSecondary(*gbufferBuffer);
            m_gbufferPass->endDynamicRendering(commandBuffer, m_currentFrame);
        }
        if (lightingBuffer) {
            m_lightingPass->beginDynamicRendering(commandBuffer, *m_sceneRenderTarget, imageIndex);
            commandBuffer->executeSecondary(*lightingBuffer);
            m_lightingPass->endDynamicRendering(commandBuffer);
        }
        if (skyboxBuffer) {
            m_skyboxPass->beginDynamicRendering(commandBuffer, *m_sceneRenderTarget, imageIndex, m_currentFrame);
            commandBuffer->executeSecondary(*skyboxBuffer);
            m_skyboxPass->endDynamicRendering(commandBuffer);
        }
        if (instancedShapesBuffer) {
            m_instancedShapesPass->beginDynamicRendering(commandBuffer, *m_sceneRenderTarget, imageIndex, m_currentFrame);
            commandBuffer->executeSecondary(*instancedShapesBuffer);
            m_instancedShapesPass->endDynamicRendering(commandBuffer);
        }
        if (stencilBorderBuffer) {
            m_stencilBorderPass->beginDynamicRendering(commandBuffer, *m_sceneRenderTarget, imageIndex);
            commandBuffer->executeSecondary(*stencilBorderBuffer);
            m_stencilBorderPass->endDynamicRendering(commandBuffer);
        }

        // Transition to shader read layout for OFFSCREEN mode so ImGui can sample it
        if (m_sceneRenderTarget->requiresSamplingTransition()) {
            m_sceneRenderTarget->transitionToShaderReadLayout(commandBuffer, imageIndex);
        }

        RAPTURE_PROFILE_GPU_COLLECT(commandBuffer->getCommandBufferVk());
    }

    if (commandBuffer->end() != VK_SUCCESS) {
        RP_CORE_ERROR("failed to record command buffer!");
        return;
    }
}

} // namespace Rapture

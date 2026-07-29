#include "DeferredRenderer.h"

#include "buffers/command_buffers/CommandPool.h"
#include "window_context/Application.h"

#include "logging/Log.h"
#include "logging/TracyProfiler.h"

#include "components/TerrainComponent.h"
#include "jobs/InplaceFunction.h"
#include "jobs/Job.h"
#include "jobs/JobCommon.h"
#include "jobs/JobSystem.h"
#include "renderer/shadows/ShadowCommon.h"
#include <cstdio>

namespace Rapture {

// Maximum number of lights supported
static constexpr uint32_t MAX_LIGHTS = 16;

static Counter s_cmdCounter{};

DeferredRenderer::DeferredRenderer(RenderContext renderContext, const RendererConfig &config) : Renderer(renderContext, config)
{
    setupCommandResources();
    createRenderTarget();

    if (m_config.enableAccelerationStructures) {
        m_rtInstanceData = std::make_unique<RtInstanceData>(m_renderContext);
        m_dynamicDiffuseGI = std::make_unique<DynamicDiffuseGI>(Application::getInstance().getFramesInFlight());
    }

    recreateRenderPasses();

    m_swapchainRecreatedConn = m_swapChain->onRecreated.connect([this]() { onSwapChainRecreated(); });
}

DeferredRenderer::~DeferredRenderer()
{
    m_renderContext.vulkanContext->waitIdle();

    m_skyboxPass.reset();
    m_stencilBorderPass.reset();
    m_lightingPass.reset();
    m_ambientOcclusionPass.reset();
    m_gbufferPass.reset();
    m_instancedShapesPass.reset();
    m_dynamicDiffuseGI.reset();
    m_rtInstanceData.reset();

    // Clean up scene render target
    m_sceneRenderTarget.reset();

    // Clean up queues
    m_graphicsQueue.reset();
    m_presentQueue.reset();

    // Clean up swapchain
    m_swapChain.reset();
}

void DeferredRenderer::drawFrame(Scene &activeScene, Entity camera, const RenderSettings &settings)
{

    RAPTURE_PROFILE_FUNCTION();

    if (m_viewportResizePending) {
        processPendingViewportResize();
    }

    m_currentFrame = Application::getInstance().getFrameInFlightIndex();

    m_giActive = m_config.enableAccelerationStructures && settings.useGlobalIllumination();
    m_lightingFlags = settings.flags;

    // For PRESENTATION mode, we need to acquire a swapchain image
    // For OFFSCREEN mode, we just use m_currentFrame as the target index
    uint32_t imageIndex = m_currentFrame;

    if (m_config.targetType == SceneRenderTarget::TargetType::SWAPCHAIN) {
        int imageIndexi = m_swapChain->acquireImage(m_currentFrame);
        if (imageIndexi == -1) {
            return;
        }
        imageIndex = static_cast<uint32_t>(imageIndexi);
    }

    if (m_rtInstanceData) {
        m_rtInstanceData->update(activeScene);
    }

    if (m_giActive) {
        JobSystem &system = jobs();
        system.run(JobDeclaration(
            [ddgi = m_dynamicDiffuseGI.get(), scenePtr = &activeScene, m_currentFrame = m_currentFrame](JobContext &ctx) {
                (void)ctx;
                ddgi->populateProbesCompute(*scenePtr, m_currentFrame);
            },
            JobPriority::NORMAL, QueueAffinity::COMPUTE, nullptr, "DDGI POPULATE"));
    }

    auto pool = m_renderContext.commandPoolManager->getCommandPool(m_commandPoolHash, m_currentFrame);
    auto commandBuffer = pool->getPrimaryCommandBuffer();

    recordCommandBuffer(commandBuffer, activeScene, camera, imageIndex, settings);

    VkSemaphore frWaitSemaphores[1];
    VkSemaphore frSignalSemaphores[1];
    VkPipelineStageFlags frWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    if (m_config.targetType == SceneRenderTarget::TargetType::SWAPCHAIN) {
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
            m_swapChain->onRecreationRequested.fire();
            return;
        } else if (result != VK_SUCCESS) {
            RP_CORE_ERROR("failed to present swap chain image!");
            return;
        }
    } else {
        m_graphicsQueue->addToBatch(commandBuffer);
    }

    m_lastRenderedFrame = imageIndex;
}

void DeferredRenderer::onSwapChainRecreated()
{
    // Wait for all operations to complete
    m_renderContext.vulkanContext->waitIdle();
    jobs().waitFor(s_cmdCounter, 0);

    // In PRESENTATION mode, the render target is backed by swapchain, so we need to recreate everything
    // In OFFSCREEN mode, only update the swapchain reference (image count may have changed)
    if (m_config.targetType == SceneRenderTarget::TargetType::SWAPCHAIN) {
        m_width = static_cast<float>(m_swapChain->getExtent().width);
        m_height = static_cast<float>(m_swapChain->getExtent().height);

        // Recreate the swapchain-backed render target
        m_sceneRenderTarget = std::make_unique<SceneRenderTarget>(m_swapChain);

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

void DeferredRenderer::resizeRenderTarget(uint32_t width, uint32_t height)
{
    if (m_config.targetType == SceneRenderTarget::TargetType::SWAPCHAIN) {
        m_framebufferNeedsResize = true;
        return;
    }

    m_pendingViewportWidth = width;
    m_pendingViewportHeight = height;
    m_viewportResizePending = true;
}

void DeferredRenderer::createRenderTarget()
{
    if (m_config.targetType == SceneRenderTarget::TargetType::OFFSCREEN) {
        // Create offscreen render target for Editor mode
        // Use BGRA8 SRGB format (matches typical swapchain format)
        m_sceneRenderTarget = std::make_unique<SceneRenderTarget>(
            static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), Application::getInstance().getFramesInFlight(),
            TextureFormat::RGBA16F, m_config.allowReadback);
        RP_CORE_INFO("Created OFFSCREEN render target for Editor mode");
    } else {
        // Create swapchain-backed render target for Standalone mode
        m_sceneRenderTarget = std::make_unique<SceneRenderTarget>(m_swapChain);
        RP_CORE_INFO("Created SWAPCHAIN-backed render target for Standalone mode");
    }
}

void DeferredRenderer::createSceneColorTextures()
{
    m_sceneColorHdrTextures.clear();

    TextureSpecification spec;
    spec.width = static_cast<uint32_t>(m_width);
    spec.height = static_cast<uint32_t>(m_height);
    spec.format = TextureFormat::RGBA16F;
    spec.type = TextureType::TEXTURE2D;
    spec.srgb = false;

    for (uint32_t i = 0; i < Application::getInstance().getFramesInFlight(); i++) {
        m_sceneColorHdrTextures.push_back(std::make_unique<Texture>(spec));
    }
}

RenderPassContext DeferredRenderer::buildPassContext(Scene &activeScene, Entity camera, uint32_t imageIndex,
                                                     const RenderSettings &settings)
{
    m_passTargets.gbufferNormalMotion = m_gbufferPass->getNormalTexture(m_currentFrame);
    m_passTargets.gbufferBaseColor = m_gbufferPass->getAlbedoTexture(m_currentFrame);
    m_passTargets.gbufferMaterial = m_gbufferPass->getMaterialTexture(m_currentFrame);
    m_passTargets.gbufferShadingModel = m_gbufferPass->getShadingModelTexture(m_currentFrame);
    m_passTargets.depthStencil = m_gbufferPass->getDepthTexture(m_currentFrame);
    m_passTargets.sceneColorHdr = m_sceneColorHdrTextures[m_currentFrame].get();
    m_passTargets.ambientOcclusion = m_ambientOcclusionPass->getOcclusionTexture(m_currentFrame);

    RenderPassContext context;
    context.scene = &activeScene;
    context.camera = camera;
    context.renderTarget = m_sceneRenderTarget.get();
    context.targets = &m_passTargets;
    context.settings = &settings;
    context.frameInFlight = m_currentFrame;
    context.imageIndex = imageIndex;

    return context;
}

void DeferredRenderer::recreateRenderPasses()
{
    jobs().waitFor(s_cmdCounter, 0);

    m_skyboxPass.reset();
    m_stencilBorderPass.reset();
    m_lightingPass.reset();
    m_ambientOcclusionPass.reset();
    m_gbufferPass.reset();
    m_instancedShapesPass.reset();

    VkFormat presentFormat = m_sceneRenderTarget->getFormat();

    createSceneColorTextures();
    VkFormat hdrFormat = m_sceneColorHdrTextures[0]->getFormat();

    const uint32_t framesInFlight = Application::getInstance().getFramesInFlight();

    m_gbufferPass = std::make_unique<GBufferPass>(m_width, m_height, framesInFlight);

    m_ambientOcclusionPass = std::make_unique<GroundTruthAmbientOcclusionPass>(
        static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), framesInFlight);

    m_lightingPass = std::make_unique<LightingPass>(m_width, m_height, m_dynamicDiffuseGI.get(), hdrFormat);

    m_stencilBorderPass =
        std::make_unique<StencilBorderPass>(m_width, m_height, framesInFlight, m_gbufferPass->getDepthTextures(), hdrFormat);

    m_instancedShapesPass =
        std::make_unique<InstancedShapesPass>(m_width, m_height, framesInFlight, m_gbufferPass->getDepthTextures(), hdrFormat);

    m_skyboxPass = std::make_unique<SkyboxPass>(m_gbufferPass->getDepthTextures(), hdrFormat);

    m_compositePass = std::make_unique<CompositePass>(m_width, m_height, presentFormat);
}

void DeferredRenderer::processPendingViewportResize()
{
    uint32_t width = m_pendingViewportWidth;
    uint32_t height = m_pendingViewportHeight;
    m_viewportResizePending = false;

    if (static_cast<uint32_t>(m_width) == width && static_cast<uint32_t>(m_height) == height) {
        return; // No change
    }

    m_renderContext.vulkanContext->waitIdle();

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
    auto &vc = *m_renderContext.vulkanContext;

    CommandPoolConfig config = {};
    config.queueFamilyIndex = vc.getGraphicsQueueIndex();
    config.flags = 0;
    m_commandPoolHash = m_renderContext.commandPoolManager->createCommandPool(config);
}

void DeferredRenderer::recordCommandBuffer(CommandBuffer *commandBuffer, Scene &activeScene, Entity camera, uint32_t imageIndex,
                                           const RenderSettings &settings)
{

    RAPTURE_PROFILE_FUNCTION();

    Entity environment = activeScene.environmentEntity();
    if (environment.hasComponent<SkyboxComponent>()) {
        auto &skyboxComp = environment.getComponent<SkyboxComponent>();
        if (!m_skyboxPass->hasActiveSkybox()) {
            m_skyboxPass->setSkyboxTexture(skyboxComp.skyboxTexture.get());
        }
        m_skyboxPass->setSkyIntensity(skyboxComp.skyIntensity);
    }

    if (commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to begin recording command buffer!");
        return;
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "DeferredRenderer Frame");

        auto &registry = activeScene.getRegistry();
        auto lightView = registry.view<TransformComponent, ShadowComponent>();
        auto cascadedShadowView = registry.view<DirectionalLightComponent, TransformComponent, CascadedShadowComponent>();

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
                Entity lightEntity(entity, &activeScene);
                auto *light = Light_tryGetLight(lightEntity);
                if (light == nullptr) {
                    continue;
                }
                auto &transformComp = lightView.get<TransformComponent>(entity);
                auto &shadowComp = lightView.get<ShadowComponent>(entity);

                bool shouldUpdateShadow =
                    shadowComp.needsUpdate(*light, transformComp) || Light_getLightType(lightEntity) == LightType::DIRECTIONAL;

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
                Entity lightEntity(entity, &activeScene);
                auto &lightComp = cascadedShadowView.get<DirectionalLightComponent>(entity);
                auto &transformComp = cascadedShadowView.get<TransformComponent>(entity);
                auto &shadowComp = cascadedShadowView.get<CascadedShadowComponent>(entity);

                bool shouldUpdateShadow =
                    shadowComp.needsUpdate(lightComp, transformComp) || Light_getLightType(lightEntity) == LightType::DIRECTIONAL;

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

        RenderPassContext context = buildPassContext(activeScene, camera, imageIndex, settings);
        context.terrain = terrain;

        s_cmdCounter.increment(3); // GBuffer, Lighting, Skybox

        JobSystem &system = jobs();

        // Prepare GBuffer Pass data

        SecondaryBufferInheritance gbufferInheritance;
        auto fbSpec = GBufferPass::getFramebufferSpecification();
        gbufferInheritance.colorFormats = fbSpec.colorAttachments;
        gbufferInheritance.depthFormat = fbSpec.depthAttachment;
        gbufferInheritance.stencilFormat = fbSpec.stencilAttachment;

        // Inheritance is resolved here rather than inside the jobs, since it fills each pass's cached attachments
        SecondaryBufferInheritance lightingInheritance = m_lightingPass->getInheritance(context);
        SecondaryBufferInheritance skyboxInheritance = m_skyboxPass->getInheritance(context);

        system.run(JobDeclaration(
            [&gbufferBuffer, &context, gbufferInheritance, gbufferPass = m_gbufferPass.get()](JobContext &ctx) {
                (void)ctx;
                gbufferBuffer = gbufferPass->record(context, gbufferInheritance);
            },
            JobPriority::HIGH, QueueAffinity::ANY, &s_cmdCounter, "GBUFFER"));

        system.run(JobDeclaration(
            [&lightingBuffer, &context, lightingInheritance, lightingPass = m_lightingPass.get()](JobContext &ctx) {
                (void)ctx;
                lightingBuffer = lightingPass->record(context, lightingInheritance);
            },
            JobPriority::HIGH, QueueAffinity::ANY, &s_cmdCounter, "LIGHTING"));

        system.run(JobDeclaration(
            [&skyboxBuffer, &context, skyboxInheritance, skyboxPass = m_skyboxPass.get()](JobContext &ctx) {
                (void)ctx;
                skyboxBuffer = skyboxPass->record(context, skyboxInheritance);
            },
            JobPriority::HIGH, QueueAffinity::ANY, &s_cmdCounter, "SKYBOX"));

        // TODO: instanced shapes and the stencil border are disabled until they are converted to RenderPass
        // SecondaryBufferInheritance instancedInheritance;
        // instancedInheritance.colorFormats = {hdrFormat};
        // instancedInheritance.depthFormat = m_gbufferPass->getDepthTexture()->getFormat();
        //
        // system.run(JobDeclaration(
        //     [&instancedShapesBuffer, scenePtr = &activeScene, camera, sceneColorHdr,
        //      m_currentFrame = m_currentFrame, instancedInheritance,
        //      instancedShapesPass = m_instancedShapesPass.get()](JobContext &ctx) {
        //         (void)ctx;
        //         instancedShapesBuffer =
        //             instancedShapesPass->recordSecondary(*scenePtr, camera, sceneColorHdr, m_currentFrame, instancedInheritance);
        //     },
        //     JobPriority::HIGH, QueueAffinity::ANY, &s_cmdCounter, "INSTANCED_SHAPES"));

        {

            RAPTURE_PROFILE_SCOPE("command buffer Wait");
            system.waitFor(s_cmdCounter, 0);
        }
        // Here we wait for all of them to be finished (if in parallel)

        if (gbufferBuffer) {
            RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "GBuffer Pass");
            m_gbufferPass->beginRendering(context, commandBuffer);
            commandBuffer->executeSecondary(*gbufferBuffer);
            m_gbufferPass->endRendering(commandBuffer);
        }

        {
            RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Ambient Occlusion Pass");
            m_ambientOcclusionPass->execute(context, commandBuffer);
        }

        if (lightingBuffer) {
            RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Lighting Pass");
            m_lightingPass->beginRendering(context, commandBuffer);
            commandBuffer->executeSecondary(*lightingBuffer);
            m_lightingPass->endRendering(commandBuffer);
        }
        if (skyboxBuffer) {
            RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Skybox Pass");
            m_skyboxPass->beginRendering(context, commandBuffer);
            commandBuffer->executeSecondary(*skyboxBuffer);
            m_skyboxPass->endRendering(commandBuffer);
        }
        // if (instancedShapesBuffer) {
        //     RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Instanced Shapes Pass");
        //     m_instancedShapesPass->beginDynamicRendering(commandBuffer, sceneColorHdr, m_currentFrame);
        //     commandBuffer->executeSecondary(*instancedShapesBuffer);
        //     m_instancedShapesPass->endDynamicRendering(commandBuffer);
        // }
        // if (stencilBorderBuffer) {
        //     m_stencilBorderPass->beginDynamicRendering(commandBuffer, *m_sceneRenderTarget, imageIndex);
        //     commandBuffer->executeSecondary(*stencilBorderBuffer);
        //     m_stencilBorderPass->endDynamicRendering(commandBuffer);
        // }

        {
            RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Composite Pass");

            CommandBuffer *compositeBuffer = m_compositePass->record(context, m_compositePass->getInheritance(context));
            if (compositeBuffer != nullptr) {
                m_compositePass->beginRendering(context, commandBuffer);
                commandBuffer->executeSecondary(*compositeBuffer);
                m_compositePass->endRendering(commandBuffer);
            }
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

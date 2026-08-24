#include "DeferredRenderer.h"

#include "app/Application.h"
#include "gpu/command_buffers/CommandPool.h"

#include "core/utils/Log.h"
#include "core/utils/TracyProfiler.h"

#include "core/jobs/Job.h"
#include "core/jobs/JobCommon.h"
#include "core/jobs/JobSystem.h"
#include "scene/instances/Environment.h"
#include "scene/render_data/SceneRenderData.h"

namespace Rapture {

DeferredRenderer::DeferredRenderer(RenderContext renderContext, const RendererConfig &config) : Renderer(renderContext, config)
{
    if (m_config.enableAccelerationStructures) {
        m_rtInstanceData = std::make_unique<RtInstanceData>(m_renderContext);
        m_dynamicDiffuseGI = std::make_unique<DynamicDiffuseGI>(m_config.framesInFlight);
    }

    recreateRenderPasses();
}

DeferredRenderer::~DeferredRenderer()
{
    m_renderContext.vulkanContext->waitIdle();

    m_probeDebugPass.reset();
    m_skyboxPass.reset();
    m_lightingPass.reset();
    m_ambientOcclusionPass.reset();
    m_gbufferPass.reset();
    m_dynamicDiffuseGI.reset();
    m_rtInstanceData.reset();
}

void DeferredRenderer::onResize(uint32_t width, uint32_t height)
{
    jobs().waitFor(m_cmdCounter, 0);

    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);

    recreateRenderPasses();
}

RenderPassContext DeferredRenderer::buildPassContext(const RenderPassContext &context)
{
    const uint32_t frame = context.frameInFlight;

    m_passTargets = *context.targets;
    m_passTargets.gbufferNormalMotion = m_gbufferPass->getNormalTexture(frame);
    m_passTargets.gbufferBaseColor = m_gbufferPass->getAlbedoTexture(frame);
    m_passTargets.gbufferMaterial = m_gbufferPass->getMaterialTexture(frame);
    m_passTargets.gbufferShadingModel = m_gbufferPass->getShadingModelTexture(frame);
    m_passTargets.ambientOcclusion = m_ambientOcclusionPass->getOcclusionTexture(frame);

    RenderPassContext passContext = context;
    passContext.targets = &m_passTargets;

    return passContext;
}

void DeferredRenderer::recreateRenderPasses()
{
    jobs().waitFor(m_cmdCounter, 0);

    m_probeDebugPass.reset();
    m_skyboxPass.reset();
    m_lightingPass.reset();
    m_ambientOcclusionPass.reset();
    m_gbufferPass.reset();

    const VkFormat hdrFormat = m_config.sceneColorFormat;
    const uint32_t framesInFlight = m_config.framesInFlight;

    m_gbufferPass = std::make_unique<GBufferPass>(m_width, m_height, framesInFlight);

    m_ambientOcclusionPass = std::make_unique<GroundTruthAmbientOcclusionPass>(static_cast<uint32_t>(m_width),
                                                                               static_cast<uint32_t>(m_height), framesInFlight);

    m_lightingPass = std::make_unique<LightingPass>(m_width, m_height, m_dynamicDiffuseGI.get(), hdrFormat);

    m_skyboxPass = std::make_unique<SkyboxPass>(GBufferPass::getFramebufferSpecification().depthAttachment, hdrFormat);

    if (m_dynamicDiffuseGI) {
        DDGIProbeDebugPassConfig probeDebugConfig{};
        probeDebugConfig.width = static_cast<uint32_t>(m_width);
        probeDebugConfig.height = static_cast<uint32_t>(m_height);
        probeDebugConfig.colorFormat = hdrFormat;
        probeDebugConfig.depthFormat = GBufferPass::getFramebufferSpecification().depthAttachment;

        m_probeDebugPass = std::make_unique<DDGIProbeDebugPass>(probeDebugConfig, m_dynamicDiffuseGI.get());
    }
}

void DeferredRenderer::recordSecondaries(const RenderPassContext &frameContext, JobContext &jobContext)
{
    RAPTURE_PROFILE_SCOPE("DeferredRenderer::recordSecondaries");

    Scene &activeScene = *frameContext.scene;
    const RenderSettings &settings = *frameContext.settings;

    m_giActive = m_config.enableAccelerationStructures && settings.useGlobalIllumination();
    m_lightingFlags = settings.flags;

    if (m_rtInstanceData) {
        m_rtInstanceData->update(activeScene);
    }

    if (m_giActive) {
        JobSystem &system = jobs();
        system.run(JobDeclaration(
            [ddgi = m_dynamicDiffuseGI.get(), scenePtr = &activeScene, frame = frameContext.frameInFlight](JobContext &ctx) {
                (void)ctx;
                ddgi->populateProbesCompute(*scenePtr, frame);
            },
            JobPriority::NORMAL, QueueAffinity::COMPUTE, nullptr, "DDGI POPULATE"));
    }

    Environment *environment = activeScene.environment();
    if (environment != nullptr) {
        Texture *skybox = environment->isSkyboxEnabled() ? environment->skyboxTexture() : nullptr;
        m_skyboxPass->setSkyboxTexture(skybox);
        m_skyboxPass->setSkyIntensity(environment->skyIntensity());
    }

    {
        m_gbufferCmdBuffer = nullptr;
        m_lightingCmdBuffer = nullptr;
        m_skyboxCmdBuffer = nullptr;
        m_probeDebugCmdBuffer = nullptr;

        RenderPassContext context = buildPassContext(frameContext);

        const bool drawSkybox = m_skyboxPass->hasActiveSkybox();
        const bool drawProbes = m_probeDebugPass != nullptr && settings.showDDGIProbes();

        uint32_t passCount = 2; // GBuffer, Lighting
        passCount += drawSkybox ? 1 : 0;
        passCount += drawProbes ? 1 : 0;
        m_cmdCounter.increment(passCount);

        JobSystem &system = jobs();

        // Prepare GBuffer Pass data

        SecondaryBufferInheritance gbufferInheritance;
        auto fbSpec = GBufferPass::getFramebufferSpecification();
        gbufferInheritance.colorFormats = fbSpec.colorAttachments;
        gbufferInheritance.depthFormat = fbSpec.depthAttachment;
        gbufferInheritance.stencilFormat = fbSpec.stencilAttachment;

        // Inheritance is resolved here rather than inside the jobs, since it fills each pass's cached attachments
        SecondaryBufferInheritance lightingInheritance = m_lightingPass->getInheritance(context);

        system.run(JobDeclaration(
            [this, &context, gbufferInheritance](JobContext &ctx) {
                (void)ctx;
                m_gbufferCmdBuffer = m_gbufferPass->record(context, gbufferInheritance);
            },
            JobPriority::HIGH, QueueAffinity::ANY, &m_cmdCounter, "GBUFFER"));

        system.run(JobDeclaration(
            [this, &context, lightingInheritance](JobContext &ctx) {
                (void)ctx;
                m_lightingCmdBuffer = m_lightingPass->record(context, lightingInheritance);
            },
            JobPriority::HIGH, QueueAffinity::ANY, &m_cmdCounter, "LIGHTING"));

        if (drawSkybox) {
            SecondaryBufferInheritance skyboxInheritance = m_skyboxPass->getInheritance(context);

            system.run(JobDeclaration(
                [this, &context, skyboxInheritance](JobContext &ctx) {
                    (void)ctx;
                    m_skyboxCmdBuffer = m_skyboxPass->record(context, skyboxInheritance);
                },
                JobPriority::HIGH, QueueAffinity::ANY, &m_cmdCounter, "SKYBOX"));
        }

        if (drawProbes) {
            SecondaryBufferInheritance probeDebugInheritance = m_probeDebugPass->getInheritance(context);

            system.run(JobDeclaration(
                [this, &context, probeDebugInheritance](JobContext &ctx) {
                    (void)ctx;
                    m_probeDebugCmdBuffer = m_probeDebugPass->record(context, probeDebugInheritance);
                },
                JobPriority::HIGH, QueueAffinity::ANY, &m_cmdCounter, "DDGI PROBE DEBUG"));
        }

        {
            // yields the worker rather than blocking it, since this runs on a job itself
            RAPTURE_PROFILE_SCOPE("command buffer Wait");
            jobContext.waitFor(m_cmdCounter, 0);
        }
    }
}

void DeferredRenderer::replay(const RenderPassContext &frameContext, CommandBuffer *commandBuffer)
{
    RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "DeferredRenderer");

    RenderPassContext context = buildPassContext(frameContext);

    if (m_gbufferCmdBuffer) {
        RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "GBuffer Pass");
        m_gbufferPass->beginRendering(context, commandBuffer);
        commandBuffer->executeSecondary(*m_gbufferCmdBuffer);
        m_gbufferPass->endRendering(commandBuffer);
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Ambient Occlusion Pass");
        m_ambientOcclusionPass->execute(context, commandBuffer);
    }

    if (m_lightingCmdBuffer) {
        RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Lighting Pass");
        m_lightingPass->beginRendering(context, commandBuffer);
        commandBuffer->executeSecondary(*m_lightingCmdBuffer);
        m_lightingPass->endRendering(commandBuffer);
    }

    if (m_skyboxCmdBuffer) {
        RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Skybox Pass");
        m_skyboxPass->beginRendering(context, commandBuffer);
        commandBuffer->executeSecondary(*m_skyboxCmdBuffer);
        m_skyboxPass->endRendering(commandBuffer);
    }

    if (m_probeDebugCmdBuffer) {
        RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "DDGI Probe Debug Pass");
        m_probeDebugPass->beginRendering(context, commandBuffer);
        commandBuffer->executeSecondary(*m_probeDebugCmdBuffer);
        m_probeDebugPass->endRendering(commandBuffer);
    }
}

} // namespace Rapture

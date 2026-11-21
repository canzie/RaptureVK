#include "renderer/gi/ddgi/DynamicDiffuseGIProbeDebugRenderer.h"

#include <rapture/core/utils/TracyProfiler.h>
#include <rapture/renderer/RenderSettings.h>

namespace Rapture {

DynamicDiffuseGIProbeDebugRenderer::DynamicDiffuseGIProbeDebugRenderer(RenderContext renderContext, const RendererConfig &config,
                                                                       VkFormat depthFormat)
    : Renderer(renderContext, config), m_depthFormat(depthFormat)
{
    createPass();
}

void DynamicDiffuseGIProbeDebugRenderer::createPass()
{
    DDGIProbeDebugPassConfig passConfig{};
    passConfig.width = static_cast<uint32_t>(m_width);
    passConfig.height = static_cast<uint32_t>(m_height);
    passConfig.colorFormat = m_config.sceneColorFormat;
    passConfig.depthFormat = m_depthFormat;

    m_pass = std::make_unique<DDGIProbeDebugPass>(passConfig);
}

void DynamicDiffuseGIProbeDebugRenderer::onResize(uint32_t width, uint32_t height)
{
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);

    createPass();
}

void DynamicDiffuseGIProbeDebugRenderer::recordSecondaries(const RenderPassContext &context, JobContext &jobContext)
{
    RAPTURE_PROFILE_SCOPE("DynamicDiffuseGIProbeDebugRenderer::recordSecondaries");

    (void)jobContext;

    m_secondary = {};
    if (!context.settings->showDDGIProbes()) {
        return;
    }

    m_secondary = m_pass->record(context, m_pass->getInheritance(context));
}

void DynamicDiffuseGIProbeDebugRenderer::replay(const RenderPassContext &context, CommandBuffer *primaryCb)
{
    if (!m_secondary.isValid()) {
        return;
    }

    RAPTURE_PROFILE_GPU_SCOPE(primaryCb->getCommandBufferVk(), "DDGI Probe Debug Pass");
    m_pass->beginRendering(context, primaryCb);
    primaryCb->executeSecondary(std::move(m_secondary));
    m_pass->endRendering(primaryCb);
}

} // namespace Rapture

#include "renderer/DepthPrepassRenderer.h"

#include "core/utils/TracyProfiler.h"

namespace Rapture {

DepthPrepassRenderer::DepthPrepassRenderer(RenderContext renderContext, const RendererConfig &config, VkFormat depthFormat)
    : Renderer(renderContext, config), m_depthFormat(depthFormat)
{
    m_pass = std::make_unique<DepthPrepass>(m_width, m_height, m_depthFormat);
}

void DepthPrepassRenderer::onResize(uint32_t width, uint32_t height)
{
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);

    m_pass = std::make_unique<DepthPrepass>(m_width, m_height, m_depthFormat);
}

void DepthPrepassRenderer::recordSecondaries(const RenderPassContext &context, JobContext &jobContext)
{
    RAPTURE_PROFILE_SCOPE("DepthPrepassRenderer::recordSecondaries");

    (void)jobContext;

    m_secondary = m_pass->record(context, m_pass->getInheritance(context));
}

void DepthPrepassRenderer::replay(const RenderPassContext &context, CommandBuffer *primaryCb)
{
    RAPTURE_PROFILE_GPU_SCOPE(primaryCb->getCommandBufferVk(), "Depth Prepass");

    if (m_secondary == nullptr) {
        return;
    }

    m_pass->beginRendering(context, primaryCb);
    primaryCb->executeSecondary(*m_secondary);
    m_pass->endRendering(primaryCb);
}

} // namespace Rapture

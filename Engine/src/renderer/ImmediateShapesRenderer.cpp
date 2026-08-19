#include "renderer/ImmediateShapesRenderer.h"

#include "core/utils/TracyProfiler.h"
#include "core/utils/rp_assert.h"

namespace Rapture {

ImmediateShapesRenderer::ImmediateShapesRenderer(RenderContext renderContext, const RendererConfig &config,
                                                 VkFormat depthFormat, ImmediateDrawList *drawList)
    : Renderer(renderContext, config), m_drawList(drawList), m_depthFormat(depthFormat)
{
    RP_ASSERT(drawList != nullptr, "Immediate shapes renderer needs a draw list to draw");

    buildPass();
}

void ImmediateShapesRenderer::buildPass()
{
    ImmediateShapesDrawPassConfig passConfig;
    passConfig.width = static_cast<uint32_t>(m_width);
    passConfig.height = static_cast<uint32_t>(m_height);
    passConfig.colorFormat = m_config.outputFormat;
    passConfig.depthFormat = m_depthFormat;
    passConfig.framesInFlight = m_config.framesInFlight;

    m_pass = std::make_unique<ImmediateShapesDrawPass>(passConfig, m_drawList);
}

void ImmediateShapesRenderer::onResize(uint32_t width, uint32_t height)
{
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);

    m_pass->onResize(width, height);
}

void ImmediateShapesRenderer::recordSecondaries(const RenderPassContext &context, JobContext &jobContext)
{
    RAPTURE_PROFILE_SCOPE("ImmediateShapesRenderer::recordSecondaries");

    (void)jobContext;

    m_secondary = m_pass->record(context, m_pass->getInheritance(context));
}

void ImmediateShapesRenderer::replay(const RenderPassContext &context, CommandBuffer *primaryCb)
{
    RAPTURE_PROFILE_GPU_SCOPE(primaryCb->getCommandBufferVk(), "Immediate Shapes");

    if (m_secondary != nullptr) {
        m_pass->beginRendering(context, primaryCb);
        primaryCb->executeSecondary(*m_secondary);
        m_pass->endRendering(primaryCb);
    }

    m_drawList->clear();
}

} // namespace Rapture

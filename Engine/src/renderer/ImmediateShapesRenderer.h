#ifndef RAPTURE__IMMEDIATE_SHAPES_RENDERER_H
#define RAPTURE__IMMEDIATE_SHAPES_RENDERER_H

#include "renderer/ImmediateDrawList.h"
#include "renderer/Renderer.h"
#include "renderer/passes/ImmediateShapesDrawPass.h"

#include <memory>

namespace Rapture {

/**
 * @brief Draws a view's submitted shapes, clearing them once they have been recorded
 */
class ImmediateShapesRenderer : public Renderer {
  public:
    ImmediateShapesRenderer(RenderContext renderContext, const RendererConfig &config, VkFormat depthFormat,
                            ImmediateDrawList *drawList);

    const char *name() const override { return "Immediate Shapes"; }

    void recordSecondaries(const RenderPassContext &context, JobContext &jobContext) override;
    void replay(const RenderPassContext &context, CommandBuffer *primaryCb) override;
    void onResize(uint32_t width, uint32_t height) override;

  private:
    void buildPass();

  private:
    std::unique_ptr<ImmediateShapesDrawPass> m_pass;
    ImmediateDrawList *m_drawList = nullptr;
    CommandBuffer *m_secondary = nullptr;
    VkFormat m_depthFormat;
};

} // namespace Rapture

#endif // RAPTURE__IMMEDIATE_SHAPES_RENDERER_H

#ifndef RAPTURE__GIZMO_RENDERER_H
#define RAPTURE__GIZMO_RENDERER_H

#include "renderer/GizmoDrawList.h"
#include "renderer/Renderer.h"
#include "renderer/passes/GizmoDrawPass.h"

#include <memory>

namespace Rapture {

/**
 * @brief Draws a view's submitted shapes, clearing them once they have been recorded
 */
class GizmoRenderer : public Renderer {
  public:
    GizmoRenderer(RenderContext renderContext, const RendererConfig &config, VkFormat depthFormat,
                            GizmoDrawList *drawList);

    const char *name() const override { return "Gizmos"; }

    void recordSecondaries(const RenderPassContext &context, JobContext &jobContext) override;
    void replay(const RenderPassContext &context, CommandBuffer *primaryCb) override;
    void onResize(uint32_t width, uint32_t height) override;

  private:
    void buildPass();

  private:
    std::unique_ptr<GizmoDrawPass> m_pass;
    GizmoDrawList *m_drawList = nullptr;
    CommandBuffer *m_secondary = nullptr;
    VkFormat m_depthFormat;
};

} // namespace Rapture

#endif // RAPTURE__GIZMO_RENDERER_H

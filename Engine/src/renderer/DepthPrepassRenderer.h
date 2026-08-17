#ifndef RAPTURE__DEPTH_PREPASS_RENDERER_H
#define RAPTURE__DEPTH_PREPASS_RENDERER_H

#include "renderer/Renderer.h"
#include "renderer/passes/DepthPrepass.h"

#include <memory>

namespace Rapture {

/**
 * @brief Lays down this view's depth for the renderers that follow it
 */
class DepthPrepassRenderer : public Renderer {
  public:
    DepthPrepassRenderer(RenderContext renderContext, const RendererConfig &config, VkFormat depthFormat);

    const char *name() const override { return "Depth Prepass"; }

    void recordSecondaries(const RenderPassContext &context, JobContext &jobContext) override;
    void replay(const RenderPassContext &context, CommandBuffer *primaryCb) override;
    void onResize(uint32_t width, uint32_t height) override;

  private:
    std::unique_ptr<DepthPrepass> m_pass;
    CommandBuffer *m_secondary = nullptr;
    VkFormat m_depthFormat;
};

} // namespace Rapture

#endif // RAPTURE__DEPTH_PREPASS_RENDERER_H

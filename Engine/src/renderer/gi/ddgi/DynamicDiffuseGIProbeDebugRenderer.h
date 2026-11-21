#ifndef RAPTURE__DYNAMIC_DIFFUSE_GI_PROBE_DEBUG_RENDERER_H
#define RAPTURE__DYNAMIC_DIFFUSE_GI_PROBE_DEBUG_RENDERER_H

#include "renderer/Renderer.h"
#include "renderer/passes/DDGIProbeDebugPass.h"

#include <memory>

namespace Rapture {

/**
 * @brief Draws the global illumination probes over the finished scene
 */
class DynamicDiffuseGIProbeDebugRenderer : public Renderer {
  public:
    DynamicDiffuseGIProbeDebugRenderer(RenderContext renderContext, const RendererConfig &config, VkFormat depthFormat);

    const char *name() const override { return "DDGI Probe Debug"; }

    void recordSecondaries(const RenderPassContext &context, JobContext &jobContext) override;
    void replay(const RenderPassContext &context, CommandBuffer *primaryCb) override;
    void onResize(uint32_t width, uint32_t height) override;

  private:
    void createPass();

  private:
    std::unique_ptr<DDGIProbeDebugPass> m_pass;
    CommandBufferLease m_secondary;
    VkFormat m_depthFormat;
};

} // namespace Rapture

#endif // RAPTURE__DYNAMIC_DIFFUSE_GI_PROBE_DEBUG_RENDERER_H

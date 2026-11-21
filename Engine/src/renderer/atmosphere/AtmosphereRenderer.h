#ifndef RAPTURE__ATMOSPHERE_RENDERER_H
#define RAPTURE__ATMOSPHERE_RENDERER_H

#include "renderer/Renderer.h"
#include "renderer/atmosphere/SkyAndAerialPerspectivePass.h"
#include "renderer/atmosphere/ViewAtmosphereLookupTablesPass.h"

#include <memory>

namespace Rapture {

/**
 * @brief Draws the scene's atmosphere for this view: the sky behind the lit scene and the haze in front of it
 */
class AtmosphereRenderer : public Renderer {
  public:
    AtmosphereRenderer(RenderContext renderContext, const RendererConfig &config);
    ~AtmosphereRenderer() override;

    const char *name() const override { return "Atmosphere"; }

    void recordSecondaries(const RenderPassContext &context, JobContext &jobContext) override;
    void replay(const RenderPassContext &context, CommandBuffer *primaryCb) override;
    void onResize(uint32_t width, uint32_t height) override;

  private:
    std::unique_ptr<ViewAtmosphereLookupTablesPass> m_lookupTablesPass;
    std::unique_ptr<SkyAndAerialPerspectivePass> m_skyAndAerialPerspectivePass;

    CommandBufferLease m_secondary;
    bool m_drawAtmosphere = false;
};

} // namespace Rapture

#endif // RAPTURE__ATMOSPHERE_RENDERER_H

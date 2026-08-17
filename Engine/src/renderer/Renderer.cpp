#include "Renderer.h"

#include "core/utils/rp_assert.h"

namespace Rapture {

Renderer::Renderer(RenderContext renderContext, const RendererConfig &config)
    : m_renderContext(renderContext), m_config(config), m_width(static_cast<float>(config.width)),
      m_height(static_cast<float>(config.height))
{
    RP_ASSERT(m_config.framesInFlight > 0, "a renderer needs at least one frame in flight");
}

} // namespace Rapture

#include "Renderer.h"

#include "utils/rp_assert.h"
#include "window_context/Application.h"
#include "window_context/vulkan_context/VulkanContext.h"

namespace Rapture {

Renderer::Renderer(RenderContext renderContext, const RendererConfig &config) : m_renderContext(renderContext), m_config(config)
{
    RP_ASSERT(m_config.framesInFlight > 0, "a renderer needs at least one frame in flight");

    auto &vc = *m_renderContext.vulkanContext;

    m_swapChain = Application::getInstance().getMainWindow().getSwapChain();
    m_graphicsQueue = vc.getGraphicsQueue();
    m_presentQueue = vc.getPresentQueue();

    m_width = static_cast<float>(m_swapChain->getExtent().width);
    m_height = static_cast<float>(m_swapChain->getExtent().height);
}

} // namespace Rapture

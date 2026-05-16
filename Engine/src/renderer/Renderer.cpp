#include "Renderer.h"

#include "window_context/vulkan_context/VulkanContext.h"

namespace Rapture {

Renderer::Renderer(RenderContext renderContext, SceneRenderTarget::TargetType targetType)
    : m_renderContext(renderContext), m_targetType(targetType)
{
    auto &vc = *m_renderContext.vulkanContext;

    m_swapChain = vc.getSwapChain();
    m_graphicsQueue = vc.getGraphicsQueue();
    m_presentQueue = vc.getPresentQueue();

    m_width = static_cast<float>(m_swapChain->getExtent().width);
    m_height = static_cast<float>(m_swapChain->getExtent().height);
}

} // namespace Rapture

#include "RenderWindow.h"

#include "events/ApplicationEvents.h"
#include "render_targets/swap_chains/SwapChain.h"
#include "utils/rp_assert.h"
#include "window_context/vulkan_context/VulkanContext.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Rapture {

RenderWindow::RenderWindow(std::unique_ptr<WindowContext> windowContext, VulkanContext &context)
    : m_windowContext(std::move(windowContext)), m_context(&context), m_instance(context.getInstance()),
      m_surface(VK_NULL_HANDLE), m_swapChain(nullptr), m_recreateListenerID(0)
{
    createSurface();
}

RenderWindow::~RenderWindow()
{
    if (m_recreateListenerID != 0) {
        ApplicationEvents::onRequestSwapChainRecreation().removeListener(m_recreateListenerID);
    }

    m_swapChain.reset();

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
}

void RenderWindow::createSurface()
{
    VkResult result = glfwCreateWindowSurface(
        m_instance, static_cast<GLFWwindow *>(m_windowContext->getNativeWindowContext()), nullptr, &m_surface);
    RP_ASSERT(result == VK_SUCCESS, "Failed to create window surface!");
}

void RenderWindow::createSwapChain(VulkanContext &context)
{
    m_swapChain = std::make_shared<SwapChain>(context.getLogicalDevice(), m_surface, context.getPhysicalDevice(),
                                              context.getQueueFamilyIndices(), m_windowContext.get());

    m_recreateListenerID = ApplicationEvents::onRequestSwapChainRecreation().addListener([this](uint32_t swapChainID) {
        if (swapChainID != m_swapChain->getID()) {
            return;
        }

        int width = 0, height = 0;
        m_windowContext->getFramebufferSize(&width, &height);
        if (width == 0 || height == 0) {
            return;
        }

        m_context->waitIdle();

        m_swapChain->recreate();
        ApplicationEvents::onSwapChainRecreated().publish(m_swapChain);
    });
}

void RenderWindow::onUpdate()
{
    m_windowContext->onUpdate();
}

} // namespace Rapture

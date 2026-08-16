#include "RenderWindow.h"

#include "gpu/command_buffers/CommandBuffer.h"
#include "core/utils/Log.h"
#include "gpu/swap_chains/SwapChain.h"
#include "core/utils/rp_assert.h"
#include "gpu/vulkan_context/VulkanContext.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <span>

namespace Rapture {

RenderWindow::RenderWindow(std::unique_ptr<WindowContext> windowContext, VulkanContext &context)
    : m_windowContext(std::move(windowContext)), m_context(&context), m_instance(context.getInstance()), m_surface(VK_NULL_HANDLE),
      m_swapChain(nullptr)
{
    createSurface();
}

RenderWindow::~RenderWindow()
{
    m_swapChain.reset();

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
}

void RenderWindow::createSurface()
{
    VkResult result = glfwCreateWindowSurface(m_instance, static_cast<GLFWwindow *>(m_windowContext->getNativeWindowContext()),
                                              nullptr, &m_surface);
    RP_ASSERT(result == VK_SUCCESS, "Failed to create window surface!");
}

void RenderWindow::createSwapChain(VulkanContext &context)
{
    m_swapChain = std::make_shared<SwapChain>(context.getLogicalDevice(), m_surface, context.getPhysicalDevice(),
                                              context.getQueueFamilyIndices(), m_windowContext.get());

    m_swapchainRecreateConn = m_swapChain->onRecreationRequested.connect([this]() {
        int width = 0, height = 0;
        m_windowContext->getFramebufferSize(&width, &height);
        if (width == 0 || height == 0) {
            return;
        }

        m_context->waitIdle();

        m_swapChain->recreate();
        m_swapChain->onRecreated.fire();
    });

    m_windowResizeConn = m_windowContext->onResize.connect([this](uint32_t width, uint32_t height) {
        (void)width;
        (void)height;
        m_framebufferResized = true;
    });
}

AcquiredFrame RenderWindow::beginFrame()
{
    if (m_windowContext->isMinimized()) {
        return {};
    }

    if (m_presentCommandPoolHash == 0) {
        CommandPoolConfig config;
        config.queueFamilyIndex = m_context->getGraphicsQueueIndex();
        config.flags = 0;
        config.threadId = 0;
        m_presentCommandPoolHash = m_context->getRenderContext().commandPoolManager->createCommandPool(config);
    }

    int imageIndex = m_swapChain->acquireImage(m_currentFrame);
    if (imageIndex == -1) {
        m_currentFrame = 0;
        m_context->getGraphicsQueue()->clear();
        return {};
    }

    m_currentImageIndex = static_cast<uint32_t>(imageIndex);

    auto pool = m_context->getRenderContext().commandPoolManager->getCommandPool(m_presentCommandPoolHash, m_currentFrame);
    m_currentCommandBuffer = pool->getPrimaryCommandBuffer();

    if (m_currentCommandBuffer->begin(0) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to begin present command buffer!");
        return {};
    }

    AcquiredFrame frame;
    frame.acquired = true;
    frame.imageIndex = m_currentImageIndex;
    frame.imageView = m_swapChain->getImageViews()[m_currentImageIndex];
    frame.commandBuffer = m_currentCommandBuffer;
    return frame;
}

void RenderWindow::endFrame()
{
    if (m_currentCommandBuffer->end() != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to record present command buffer!");
        return;
    }

    VkSemaphore imageAvailable = m_swapChain->getImageAvailableSemaphore(m_currentFrame);
    VkSemaphore renderFinished = m_swapChain->getRenderFinishedSemaphore(m_currentImageIndex);

    std::span<VkSemaphore> waitSpan(&imageAvailable, 1);
    std::span<VkSemaphore> signalSpan(&renderFinished, 1);
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    m_context->getGraphicsQueue()->submitAndFlushQueue(m_currentCommandBuffer, &signalSpan, &waitSpan, &waitStage,
                                                       m_swapChain->getInFlightFence(m_currentFrame));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;

    VkSwapchainKHR swapChains[] = {m_swapChain->getSwapChainVk()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_currentImageIndex;
    presentInfo.pResults = nullptr;

    VkResult result = m_context->getPresentQueue()->presentQueue(presentInfo);
    m_swapChain->signalImageAvailability(m_currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false;
        m_currentFrame = 0;
        m_swapChain->onRecreationRequested.fire();
        return;
    } else if (result != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to present swap chain image!");
        return;
    }

    m_currentFrame = (m_currentFrame + 1) % m_swapChain->getImageCount();
}

void RenderWindow::onUpdate()
{
    m_windowContext->onUpdate();
}

} // namespace Rapture

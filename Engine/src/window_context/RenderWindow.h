#ifndef RAPTURE__RENDER_WINDOW_H
#define RAPTURE__RENDER_WINDOW_H

#include "buffers/command_buffers/CommandPool.h"
#include "events/EventSignal.h"
#include "window_context/WindowContext.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <functional>
#include <memory>

namespace Rapture {

class VulkanContext;
class SwapChain;
class CommandBuffer;

/**
 * @brief A swapchain image acquired for the current frame, ready to record into.
 */
struct AcquiredFrame {
    bool acquired = false;
    uint32_t imageIndex = 0;
    VkImageView imageView = VK_NULL_HANDLE;
    CommandBuffer *commandBuffer = nullptr;
};

/**
 * @brief An OS window that can be rendered into.
 */
class RenderWindow {
  public:
    RenderWindow(std::unique_ptr<WindowContext> windowContext, VulkanContext &context);
    ~RenderWindow();

    RenderWindow(const RenderWindow &) = delete;
    RenderWindow &operator=(const RenderWindow &) = delete;

    void createSwapChain(VulkanContext &context);

    WindowContext *getWindowContext() const { return m_windowContext.get(); }
    VkSurfaceKHR getSurface() const { return m_surface; }
    std::shared_ptr<SwapChain> getSwapChain() const { return m_swapChain; }

    /**
     * @brief Acquire the next swapchain image and begin its command buffer.
     *
     * Returns an unacquired frame when the window is minimized or the swapchain
     * needs recreation, in which case the caller should skip the frame.
     */
    AcquiredFrame beginFrame();

    /**
     * @brief End, submit and present the command buffer from the matching beginFrame.
     */
    void endFrame();

    void onUpdate();

    using FrameCallback = std::function<void(RenderWindow &)>;
    using CloseCallback = std::function<void()>;

    /**
     * @brief Callback invoked once per frame to record and present a secondary window.
     */
    FrameCallback onFrame;

    /**
     * @brief Callback invoked once before a secondary window is destroyed.
     */
    CloseCallback onClose;

  private:
    void createSurface();

    std::unique_ptr<WindowContext> m_windowContext;
    VulkanContext *m_context;
    VkInstance m_instance;
    VkSurfaceKHR m_surface;
    std::shared_ptr<SwapChain> m_swapChain;
    EventConnection m_swapchainRecreateConn;
    EventConnection m_windowResizeConn;

    CommandPoolHash m_presentCommandPoolHash = 0;
    CommandBuffer *m_currentCommandBuffer = nullptr;
    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;
    bool m_framebufferResized = false;
};

} // namespace Rapture

#endif // RAPTURE__RENDER_WINDOW_H

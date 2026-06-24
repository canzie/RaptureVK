#ifndef RAPTURE__RENDER_WINDOW_H
#define RAPTURE__RENDER_WINDOW_H

#include "window_context/WindowContext.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <memory>

namespace Rapture {

class VulkanContext;
class SwapChain;

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

    void onUpdate();

  private:
    void createSurface();

    std::unique_ptr<WindowContext> m_windowContext;
    VulkanContext *m_context;
    VkInstance m_instance;
    VkSurfaceKHR m_surface;
    std::shared_ptr<SwapChain> m_swapChain;
    size_t m_recreateListenerID;
};

} // namespace Rapture

#endif // RAPTURE__RENDER_WINDOW_H

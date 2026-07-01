#ifndef RAPTURE__APPLICATION_H
#define RAPTURE__APPLICATION_H

#include "PlatformContext.h"
#include "RenderWindow.h"
#include "WindowContext.h"
#include "layers/LayerStack.h"
#include "scenes/Project.h"
#include "scenes/Scene.h"
#include "viewport/ViewportManager.h"
#include "vulkan_context/VulkanContext.h"

#include <memory>
#include <vector>

namespace Rapture {

class Application {
  public:
    Application(int width, int height, const char *title);

    virtual ~Application();

    void run();

    void pushLayer(Layer *layer);
    void pushOverlay(Layer *overlay);

    /**
     * @brief Create and register an additional OS window, sharing the existing device/queues.
     * @param width Initial window width.
     * @param height Initial window height.
     * @param title Window title.
     * @return Reference to the new window, valid until it is closed.
     */
    RenderWindow &createSecondaryWindow(int32_t width, int32_t height, const char *title);

    const VulkanContext &getVulkanContext() const { return *m_vulkanContext; }
    VulkanContext &getVulkanContext() { return *m_vulkanContext; }
    const RenderWindow &getMainWindow() const { return *m_mainWindow; }
    RenderWindow &getMainWindow() { return *m_mainWindow; }
    const WindowContext &getWindowContext() const { return *m_mainWindow->getWindowContext(); }
    WindowContext &getWindowContext() { return *m_mainWindow->getWindowContext(); }
    const Project &getProject() const { return *m_project; }
    ViewportManager &getViewportManager() { return *m_viewportManager; }
    const ViewportManager &getViewportManager() const { return *m_viewportManager; }

    /**
     * @brief App-wide frame-in-flight ring index, advanced once per main-loop iteration.
     * @return The slot every per-frame CPU->GPU resource should be indexed by.
     */
    uint32_t getFrameInFlightIndex() const { return m_frameInFlightIndex; }

    static Application &getInstance() { return *s_instance; }
    static const RenderContext &getRenderContext() { return s_instance->m_vulkanContext->getRenderContext(); }

  private:
    bool m_running;
    bool m_isMinimized;

    LayerStack m_layerStack;

    std::unique_ptr<Project> m_project;

    std::unique_ptr<PlatformContext> m_platformContext;

    std::unique_ptr<VulkanContext> m_vulkanContext;

    std::unique_ptr<RenderWindow> m_mainWindow;
    std::vector<std::unique_ptr<RenderWindow>> m_secondaryWindows;

    std::unique_ptr<ViewportManager> m_viewportManager;

    uint32_t m_frameInFlightIndex = 0;

    static Application *s_instance;
};

Application *CreateApplicationWindow(int width, int height, const char *title);

} // namespace Rapture

#endif // RAPTURE__APPLICATION_H

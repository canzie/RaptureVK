#ifndef RAPTURE__APPLICATION_H
#define RAPTURE__APPLICATION_H

#include "WindowContext.h"
#include "layers/LayerStack.h"
#include "scenes/Project.h"
#include "scenes/Scene.h"
#include "viewport/ViewportManager.h"
#include "vulkan_context/VulkanContext.h"

#include <memory>

namespace Rapture {

class Application {
  public:
    Application(int width, int height, const char *title);

    virtual ~Application();

    void run();

    void pushLayer(Layer *layer);
    void pushOverlay(Layer *overlay);

    const VulkanContext &getVulkanContext() const { return *m_vulkanContext; }
    VulkanContext &getVulkanContext() { return *m_vulkanContext; }
    const WindowContext &getWindowContext() const { return *m_window; }
    WindowContext &getWindowContext() { return *m_window; }
    const Project &getProject() const { return *m_project; }
    ViewportManager &getViewportManager() { return *m_viewportManager; }
    const ViewportManager &getViewportManager() const { return *m_viewportManager; }

    static Application &getInstance() { return *s_instance; }
    static const RenderContext &getRenderContext() { return s_instance->m_vulkanContext->getRenderContext(); }

  private:
    bool m_running;
    bool m_isMinimized;

    LayerStack m_layerStack;

    std::unique_ptr<Project> m_project;

    std::unique_ptr<WindowContext> m_window;

    std::unique_ptr<VulkanContext> m_vulkanContext;

    std::unique_ptr<ViewportManager> m_viewportManager;

    static Application *s_instance;
};

Application *CreateApplicationWindow(int width, int height, const char *title);

} // namespace Rapture

#endif // RAPTURE__APPLICATION_H

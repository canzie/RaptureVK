#ifndef RAPTURE__APPLICATION_H
#define RAPTURE__APPLICATION_H

#include "PlatformContext.h"
#include "RenderWindow.h"
#include "Telemetry.h"
#include "WindowContext.h"
#include "layers/LayerStack.h"
#include "scenes/Project.h"
#include "scenes/Scene.h"
#include "viewport/ViewportManager.h"
#include "vulkan_context/VulkanContext.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace Rapture {

class Application {
  public:
    Application(int width, int height, const char *title);

    virtual ~Application();

    void run();

    /**
     * @brief Stops the run loop and asks the entry point to start the editor again
     *
     * Project scoped state reaches into the asset registry, the material manager and every editor
     * panel, so switching projects restarts the process rather than tearing that down in place.
     *
     * @param projectPath The project the new process opens, empty for the default project
     */
    void requestRelaunch(const std::filesystem::path &projectPath = {});

    /**
     * @brief Stops the run loop, ending the process once the frame it is in is done
     */
    void close() { m_running = false; }

    bool isRelaunchRequested() const { return m_relaunchRequested; }
    const std::filesystem::path &getRelaunchProject() const { return m_relaunchProject; }

    /**
     * @brief Reads a project file into the running project
     * @param projectPath The .rapt to open
     * @return True if the project now holds the file's contents
     */
    bool openProject(const std::filesystem::path &projectPath);

    /**
     * @brief Writes a new project to disk, leaving the running project alone
     * @param projectDirectory The directory the project is created in
     * @param name The project's name
     * @return The new project file, empty if it could not be written
     */
    std::filesystem::path createProject(const std::filesystem::path &projectDirectory, std::string_view name);

    /**
     * @brief Whether a real project is open rather than the empty stand in
     */
    bool hasProject() const { return m_project->isValid(); }

    /**
     * @brief Hands a layer to the frame, attached
     * @param layer The layer to hold
     * @return The layer, owned by the application
     */
    Layer *pushLayer(std::unique_ptr<Layer> layer);

    /**
     * @brief Hands an overlay to the frame, attached and drawn over the layers
     * @param overlay The overlay to hold
     * @return The overlay, owned by the application
     */
    Layer *pushOverlay(std::unique_ptr<Layer> overlay);

    /**
     * @brief Finds a layer or overlay by name
     * @param name The name the layer was created with
     * @return The layer, or nullptr if no layer goes by that name
     */
    Layer *getLayer(std::string_view name) const;

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
    Project &getProject() { return *m_project; }
    ViewportManager &getViewportManager() { return *m_viewportManager; }
    const ViewportManager &getViewportManager() const { return *m_viewportManager; }
    const Telemetry &getTelemetry() const { return m_telemetry; }

    /**
     * @brief App-wide frame-in-flight ring index, advanced once per main-loop iteration.
     * @return The slot every per-frame CPU->GPU resource should be indexed by.
     */
    uint32_t getFrameInFlightIndex() const { return static_cast<uint32_t>(m_frameCount % m_framesInFlight); }

    /**
     * @brief The size of every per-frame-in-flight resource ring.
     * @return The number of frames the renderer keeps in flight.
     */
    uint32_t getFramesInFlight() const { return m_framesInFlight; }

    /**
     * @brief Frames elapsed since startup, never wrapping.
     * @return The frame number.
     */
    uint64_t getMonotonicFrameCount() const { return m_frameCount; }

    static Application &getInstance() { return *s_instance; }
    static const RenderContext &getRenderContext() { return s_instance->m_vulkanContext->getRenderContext(); }

  private:
    /**
     * @brief Samples the current hardware readings into m_telemetry
     */
    void pollTelemetry();

  private:
    bool m_running;
    bool m_isMinimized;
    bool m_relaunchRequested = false;
    std::filesystem::path m_relaunchProject;

    LayerStack m_layerStack;

    std::unique_ptr<Project> m_project;

    std::unique_ptr<PlatformContext> m_platformContext;

    std::unique_ptr<VulkanContext> m_vulkanContext;

    std::unique_ptr<RenderWindow> m_mainWindow;
    std::vector<std::unique_ptr<RenderWindow>> m_secondaryWindows;

    std::unique_ptr<ViewportManager> m_viewportManager;

    uint64_t m_frameCount = 0;
    uint32_t m_framesInFlight = 1;

    Telemetry m_telemetry;
    float m_telemetryPollAccum = 0.0f;

    static Application *s_instance;
};

Application *CreateApplicationWindow(int width, int height, const char *title);

} // namespace Rapture

#endif // RAPTURE__APPLICATION_H

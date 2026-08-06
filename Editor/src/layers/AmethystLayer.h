#ifndef RAPTURE__AMETHYST_LAYER_H
#define RAPTURE__AMETHYST_LAYER_H

#include "layers/Layer.h"
#include "layers/panels/common.h"

#include <amethyst/Amethyst.h>
#include <amethyst__vk13_glfw.h>

#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/command_buffers/CommandPool.h"
#include "events/EventSignal.h"
#include "layers/BottomBar.h"
#include "layers/workspaces/Workspace.h"
#include <memory>
#include <vector>

namespace Rapture {
class SwapChain;
class RenderWindow;
}

class FileBrowser;
class ProjectLauncher;

class AmethystLayer : public Rapture::Layer {
  public:
    AmethystLayer();
    ~AmethystLayer();

    virtual void onAttach(void) override;
    virtual void onDetach(void) override;
    virtual void onUpdate(float dt) override;

  private:
    void setupMenuBar(glm::vec2 screenSize);
    void setupWorkspaces(glm::vec2 screenSize);

    /**
     * @brief Points a launcher's actions at this layer
     * @param launcher The launcher to wire up
     */
    void wireLauncher(ProjectLauncher &launcher);

    /**
     * @brief Opens the launcher in its own window, leaving the open project untouched
     */
    void openLauncherWindow();

    /**
     * @brief Creates a project beside the executable and restarts the editor into it
     * @param name The new project's name
     */
    void createProject(std::string_view name);

    /**
     * @brief Records a project as the most recent and restarts the editor into it
     * @param projectPath The project file to open
     */
    void launchProject(const std::filesystem::path &projectPath);
    VkDescriptorPool createUiDescriptorPool(void);
    void beginDynamicRendering(Rapture::CommandBuffer *commandBuffer, VkImageView targetImageView, uint32_t imageIndex,
                               const Rapture::SwapChain &swapChain);
    void endDynamicRendering(Rapture::CommandBuffer *commandBuffer, uint32_t imageIndex, const Rapture::SwapChain &swapChain);
    void onResize(const Rapture::SwapChain &swapChain);

    struct SecondaryWindowContext {
        Rapture::RenderWindow *renderWindow = nullptr;
        Amethyst::Window window;
        Rapture::EventConnection swapchainRecreatedConn;
        // Declared last so it is destroyed while the Window it built into is still alive.
        std::shared_ptr<void> content;
    };

    /**
     * @brief Fills a secondary window, returning whatever must stay alive for as long as it is open
     */
    using SecondaryWindowBuilder = std::function<std::shared_ptr<void>(Amethyst::Window &, const std::function<void()> &close)>;

    PanelServices buildServices(void);
    void openSecondaryWindow(int32_t width, int32_t height, std::string_view title, SecondaryWindowBuilder build);
    void openFileExplorer(FileBrowser::Mode mode, std::function<void(const std::filesystem::path &)> onConfirm);
    void openDemoWindow(void);
    void drawSecondaryWindow(SecondaryWindowContext &context, Rapture::RenderWindow &window);
    void closeSecondaryWindow(SecondaryWindowContext *context);

  private:
    float m_Time = 0.0f;

    // Amethyst components
    VkDescriptorPool m_uiDescriptorPool = VK_NULL_HANDLE;
    Amethyst::AmVulkanBackend m_backend;
    Amethyst::AmethystContext m_amCtx;
    Amethyst::Window m_window;
    Amethyst::Frame *m_backgroundFrame = nullptr;
    Amethyst::MenuBar *m_menuBar = nullptr;
    Amethyst::TabBar *m_workspaceTabBar = nullptr;
    std::unique_ptr<ProjectLauncher> m_startupLauncher;
    std::unique_ptr<BottomBar> m_bottomBar;

    int m_activeWorkspaceIndex = 0;
    std::vector<std::unique_ptr<Workspace>> m_workspaces;

    std::vector<std::unique_ptr<SecondaryWindowContext>> m_secondaryWindows;

    Rapture::EventConnection m_mainSwapchainRecreatedConn;

};

#endif // RAPTURE__AMETHYST_LAYER_H

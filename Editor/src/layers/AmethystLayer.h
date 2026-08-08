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

#include <glm/vec2.hpp>
#include <memory>
#include <unordered_map>
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
    void setupWorkspaces(void);

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

    /**
     * @brief Opens an asset in a workspace tab of its own, focusing the tab it is already open in.
     * @param handle The asset to open, whose type picks the workspace it opens in.
     */
    void openAssetWorkspace(Rapture::AssetHandle handle);

    /**
     * @brief Destroys the workspace a closed tab held.
     * @param content The closed tab's content frame.
     */
    void closeWorkspace(Amethyst::Instance *content);

    /**
     * @brief Sizes a workspace's docking layer to the space below the tab bar.
     */
    void sizeDockingLayer(Workspace &workspace);

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
    // the asset-backed workspaces among the above, so opening an already open asset focuses its tab
    std::unordered_map<Rapture::AssetHandle, Workspace *> m_assetWorkspaces;

    std::vector<std::unique_ptr<SecondaryWindowContext>> m_secondaryWindows;

    Rapture::EventConnection m_mainSwapchainRecreatedConn;

};

#endif // RAPTURE__AMETHYST_LAYER_H

#ifndef RAPTURE__AMETHYST_LAYER_H
#define RAPTURE__AMETHYST_LAYER_H

#include "app/EditorConfig.h"
#include "app/Layer.h"
#include "context/EditorContext.h"

#include <amethyst/Amethyst.h>
#include <amethyst__vk13_glfw.h>

#include "ShortcutRegistry.h"
#include "core/events/EventSignal.h"
#include "gpu/command_buffers/CommandBuffer.h"
#include "gpu/command_buffers/CommandPool.h"
#include "layers/BottomBar.h"

#include <glm/vec2.hpp>
#include <memory>
#include <vector>

namespace Rapture {
class SwapChain;
class RenderWindow;
} // namespace Rapture

class ProjectLauncher;
class WorkspaceHost;

class AmethystLayer : public Rapture::Layer {
  public:
    AmethystLayer();
    ~AmethystLayer();

    virtual void onAttach(void) override;
    virtual void onDetach(void) override;
    virtual void onUpdate(float dt) override;

  private:
    /**
     * @brief Opens the editor's secondary windows and dialogs through the layer that draws them
     */
    class LayerWindowHost : public WindowHostBase {
      public:
        explicit LayerWindowHost(AmethystLayer &layer) : m_layer(&layer) {}

        void openSecondaryWindow(int32_t width, int32_t height, std::string_view title,
                                 std::function<void(Amethyst::Window &)> build) override;
        void openFileDialog(FileDialogMode mode, std::function<void(const std::filesystem::path &)> onConfirm) override;
        void openImportPanel(const std::filesystem::path &source, const std::filesystem::path &outputFolder) override;

      private:
        AmethystLayer *m_layer = nullptr;
    };

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

  private:
    void setupMenuBar(void);
    void setupWorkspaces(void);

    /**
     * @brief Fills in what every part of the editor reaches
     */
    void setupEditorContext(void);

    /**
     * @brief The window space below the workspace tabs and above the bottom bar
     * @param screenSize Size of the main window in pixels
     */
    void placeWorkspaceContent(glm::vec2 screenSize);

    /**
     * @brief Writes which workspaces are open into the project, then saves it
     */
    void storeEditorState(void);

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

    void updateShortcuts();

    void openSecondaryWindow(int32_t width, int32_t height, std::string_view title, SecondaryWindowBuilder build);
    void openFileDialog(FileDialogMode mode, std::function<void(const std::filesystem::path &)> onConfirm);
    void openImportPanel(const std::filesystem::path &source, const std::filesystem::path &outputFolder);
    void openDemoWindow(void);
    void drawSecondaryWindow(SecondaryWindowContext &context, Rapture::RenderWindow &window);
    void closeSecondaryWindow(SecondaryWindowContext *context);

  private:
    // Amethyst components
    VkDescriptorPool m_uiDescriptorPool = VK_NULL_HANDLE;
    Amethyst::AmVulkanBackend m_backend;
    Amethyst::AmethystContext m_amCtx;
    Amethyst::Window m_window;
    Amethyst::Frame *m_backgroundFrame = nullptr;
    Amethyst::MenuBar *m_menuBar = nullptr;
    Amethyst::TabBar *m_workspaceTabBar = nullptr;
    ShortcutRegistry m_shortcutRegistry;
    std::unique_ptr<Rapture::Input> m_input;

    EditorConfig m_config;
    UiTextures m_textures{m_backend};
    LayerWindowHost m_windowHost{*this};
    EditorContext m_editorContext;

    std::unique_ptr<WorkspaceHost> m_workspaceHost;
    std::unique_ptr<ProjectLauncher> m_startupLauncher;
    std::unique_ptr<BottomBar> m_bottomBar;
    Rapture::EventConnection m_activeWorkspaceChangedConn;

    std::vector<std::unique_ptr<SecondaryWindowContext>> m_secondaryWindows;

    std::filesystem::path m_lastExplorerDirectory;

    Rapture::EventConnection m_mainSwapchainRecreatedConn;
};

#endif // RAPTURE__AMETHYST_LAYER_H

#ifndef RAPTURE__AMETHYST_LAYER_H
#define RAPTURE__AMETHYST_LAYER_H

#include "app/EditorConfig.h"
#include "context/EditorContext.h"
#include <rapture/app/Layer.h>

#include <amethyst/Amethyst.h>
#include <amethyst__vk13_glfw.h>

#include "ShortcutRegistry.h"
#include "layers/BottomBar.h"
#include <rapture/core/events/EventSignal.h>
#include <rapture/gpu/command_buffers/CommandBuffer.h>
#include <rapture/gpu/command_buffers/CommandPool.h>

#include <glm/vec2.hpp>
#include <memory>
#include <vector>

namespace Rapture {
class SwapChain;
class RenderWindow;
} // namespace Rapture

class EditorCommands;
class ProjectLauncher;
class WindowManager;
class WorkspaceManager;

class AmethystLayer : public Rapture::Layer {
  public:
    AmethystLayer();
    ~AmethystLayer();

    virtual void onAttach(void) override;
    virtual void onDetach(void) override;
    virtual void onUpdate(float dt) override;

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

    /**
     * @brief Reads this frame's editor camera controls
     */
    void readEditorCameraInput();

    /**
     * @brief Holds the cursor while an editor camera wants it and frees it after
     */
    void applyEditorCameraCursor();

    void openDemoWindow(void);

    /**
     * @brief Draws a window other than the main one into its render window
     * @param window The UI window to draw
     * @param renderWindow The render window it is presented through
     */
    void drawSecondaryWindow(Amethyst::Window &window, Rapture::RenderWindow &renderWindow);

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
    EditorContext m_editorContext;
    EditorCameraInput m_cameraInput;
    bool m_editorCameraHoldsCursor = false;

    std::unique_ptr<WindowManager> m_windowManager;
    std::unique_ptr<WorkspaceManager> m_workspaceManager;
    std::unique_ptr<ProjectLauncher> m_startupLauncher;
    std::unique_ptr<BottomBar> m_bottomBar;
    std::unique_ptr<EditorCommands> m_commands;
    Rapture::EventConnection m_activeWorkspaceChangedConn;

    Rapture::EventConnection m_mainSwapchainRecreatedConn;
};

#endif // RAPTURE__AMETHYST_LAYER_H

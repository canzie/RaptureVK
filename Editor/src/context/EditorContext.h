#ifndef RAPTURE__EDITOR_CONTEXT_H
#define RAPTURE__EDITOR_CONTEXT_H

#include "ShortcutRegistry.h"

#include <amethyst/Amethyst.h>
#include <assets/asset_manager/AssetCommon.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>

namespace Amethyst {
class AmVulkanBackend;
} // namespace Amethyst

namespace Rapture {
class Project;
class Scene;
class Texture;
class Viewport;
class World;
} // namespace Rapture

class EditorConfig;
class EntitySelection;

/**
 * @brief Whether a file dialog opens an existing file or names a new one
 */
enum FileDialogMode {
    FILE_DIALOG_OPEN,
    FILE_DIALOG_SAVE,
    FILE_DIALOG_COUNT
};

/**
 * @brief Opens the windows and dialogs that live outside any workspace
 */
class WindowHostBase {
  public:
    virtual ~WindowHostBase() = default;

    /**
     * @brief Opens a window of the editor's own, filled by a callback
     * @param width Width of the window in pixels
     * @param height Height of the window in pixels
     * @param title Text shown in the window's title bar
     * @param build Fills the window, called once before it is first drawn
     */
    virtual void openSecondaryWindow(int32_t width, int32_t height, std::string_view title,
                                     std::function<void(Amethyst::Window &)> build) = 0;

    /**
     * @brief Opens the dialog a file is chosen through
     * @param mode Whether the dialog opens an existing file or names a new one
     * @param onConfirm Called with the chosen path, not called if the dialog is dismissed
     */
    virtual void openFileDialog(FileDialogMode mode, std::function<void(const std::filesystem::path &)> onConfirm) = 0;

    /**
     * @brief Opens the panel a source file is imported through
     * @param source The file to import
     * @param outputFolder The folder the imported assets are written to
     */
    virtual void openImportPanel(const std::filesystem::path &source, const std::filesystem::path &outputFolder) = 0;
};

/**
 * @brief Holds the open workspaces
 */
class WorkspaceHostBase {
  public:
    virtual ~WorkspaceHostBase() = default;

    /**
     * @brief Opens an asset in the workspace its type calls for, focusing the tab it is already in
     * @param handle The asset to open
     */
    virtual void openAsset(Rapture::AssetHandle handle) = 0;
};

/**
 * @brief Hands engine textures to the UI and takes them back
 */
class UiTextures {
  public:
    explicit UiTextures(Amethyst::AmVulkanBackend &backend) : m_backend(&backend) {}

    /**
     * @brief Makes a texture drawable by the UI
     * @param texture The texture to register, nullptr for none
     * @return The id the UI draws it by, invalid for a null texture
     */
    Amethyst::AmTextureId registerTexture(Rapture::Texture *texture);

    /**
     * @brief Hands a registered texture back
     * @param id The id to release
     */
    void unregisterTexture(Amethyst::AmTextureId id);

  private:
    Amethyst::AmVulkanBackend *m_backend = nullptr;
};

/**
 * @brief What every part of the editor can reach, held by the shell and handed down by reference.
 */
struct EditorContext {
    Amethyst::AmethystContext *amethyst = nullptr;
    WindowHostBase *windows = nullptr;
    WorkspaceHostBase *workspaces = nullptr;
    UiTextures *textures = nullptr;
    ShortcutRegistry *shortcuts = nullptr;
    EditorConfig *config = nullptr;
    Rapture::Project *project = nullptr;
};

/**
 * @brief What the panels of one workspace share, on top of what the whole editor reaches.
 */
struct WorkspaceContext {
    const EditorContext *editor = nullptr;

    /// The scene the panels edit, which a workspace without a world still has
    Rapture::Scene *scene = nullptr;
    /// Set only by a workspace whose scene belongs to a world
    Rapture::World *world = nullptr;
    Rapture::Viewport *viewport = nullptr;
    /// Owned by the workspace, so what one workspace selects never reaches the panels of another
    EntitySelection *selection = nullptr;
    Amethyst::DockingLayer *contentDock = nullptr;
};

#endif // RAPTURE__EDITOR_CONTEXT_H

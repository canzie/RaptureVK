#ifndef RAPTURE__EDITOR_CONTEXT_H
#define RAPTURE__EDITOR_CONTEXT_H

#include "ShortcutRegistry.h"

#include <amethyst/Amethyst.h>
#include <rapture/assets/asset_manager/AssetCommon.h>

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
class WindowManager;
class WorkspaceManager;
class EntitySelection;

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
    WindowManager *windows = nullptr;
    WorkspaceManager *workspaces = nullptr;
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

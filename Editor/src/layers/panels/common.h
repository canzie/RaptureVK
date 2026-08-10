#ifndef RAPTURE__PANELS_COMMON_H
#define RAPTURE__PANELS_COMMON_H

#include "layers/panels/FileBrowser.h"
#include <amethyst/Amethyst.h>
#include <asset_manager/AssetCommon.h>
#include <filesystem>
#include <functional>
#include <string_view>

namespace Rapture {
class Texture;
class Scene;
class Viewport;
class World;
} // namespace Rapture

class EntitySelection;

/**
 * @brief Services injected into each panel at construction time.
 */
struct PanelServices {
    std::function<void(int32_t width, int32_t height, std::string_view title, std::function<void(Amethyst::Window &)> build)>
        openSecondaryWindow;
    std::function<void(FileBrowser::Mode mode, std::function<void(const std::filesystem::path &)> onConfirm)> openFileExplorer;
    std::function<void(const std::filesystem::path &source, const std::filesystem::path &outputFolder)> openImportPanel;
    /**
     * @brief Opens an asset in its own workspace tab, focusing the tab it is already open in.
     */
    std::function<void(Rapture::AssetHandle)> openAssetWorkspace;
    std::function<Amethyst::AmTextureId(Rapture::Texture *)> registerTexture;
    std::function<void(Amethyst::AmTextureId)> unregisterTexture;
};

struct WorkspaceContext {
    /// The scene the panels edit, which a workspace without a world still has
    Rapture::Scene *scene = nullptr;
    /// Set only by a workspace whose scene belongs to a world
    Rapture::World *world = nullptr;
    Rapture::Viewport *viewport = nullptr;
    /// Owned by the workspace, so what one workspace selects never reaches the panels of another
    EntitySelection *selection = nullptr;
    Amethyst::DockingLayer *dockingLayer = nullptr;
    PanelServices services;
};

#endif // RAPTURE__PANELS_COMMON_H

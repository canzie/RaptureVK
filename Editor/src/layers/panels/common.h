#ifndef RAPTURE__PANELS_COMMON_H
#define RAPTURE__PANELS_COMMON_H

#include "layers/panels/FileBrowser.h"
#include <amethyst/Amethyst.h>
#include <filesystem>
#include <functional>
#include <string_view>

namespace Rapture {
class Texture;
class Scene;
class Viewport;
} // namespace Rapture

/**
 * @brief Services injected into each panel at construction time.
 */
struct PanelServices {
    std::function<void(int32_t width, int32_t height, std::string_view title, std::function<void(Amethyst::Window &)> build)>
        openSecondaryWindow;
    std::function<void(FileBrowser::Mode mode, std::function<void(const std::filesystem::path &)> onConfirm)> openFileExplorer;
    std::function<void(const std::filesystem::path &source, const std::filesystem::path &outputFolder)> openImportPanel;
    std::function<Amethyst::AmTextureId(Rapture::Texture *)> registerTexture;
    std::function<void(Amethyst::AmTextureId)> unregisterTexture;
};

struct WorkspaceContext {
    Rapture::Scene *scene = nullptr;
    Rapture::Viewport *viewport = nullptr;
    Amethyst::DockingLayer *dockingLayer = nullptr;
    PanelServices services;
};

#endif // RAPTURE__PANELS_COMMON_H

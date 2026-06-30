#ifndef RAPTURE__PANEL_SERVICES_H
#define RAPTURE__PANEL_SERVICES_H

#include "layers/panels/FileBrowser.h"
#include <amethyst/Amethyst.h>
#include <filesystem>
#include <functional>
#include <string_view>

namespace Rapture {
class Texture;
}

/**
 * @brief Services injected into each panel at construction time.
 */
struct PanelServices {
    std::function<void(int32_t width, int32_t height, std::string_view title, std::function<void(Amethyst::Window &)> build)> openSecondaryWindow;
    std::function<void(FileBrowser::Mode mode, std::function<void(const std::filesystem::path &)> onConfirm)> openFileExplorer;
    std::function<void(const std::filesystem::path &path)> openImportPanel;
    std::function<Amethyst::AmTextureId(Rapture::Texture *)> registerTexture;
};

#endif // RAPTURE__PANEL_SERVICES_H

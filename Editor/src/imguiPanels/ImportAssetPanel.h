#ifndef RP_EDITOR__IMPORT_ASSET_PANEL_H
#define RP_EDITOR__IMPORT_ASSET_PANEL_H

#include "AssetManager/AssetCommon.h"

#include <filesystem>
#include <imgui.h>

/*
 * @brief Panel to import a given file into an asset
 *
 */
class ImportAssetPanel {
  public:
    ImportAssetPanel(Rapture::AssetType type, std::filesystem::path &filepath);
    void render();

  private:
    // Renders main header showing information like the filepath
    void renderHeader();

    // Core components, will consist of the components below and a scrollingframe
    void renderBody();
    // Render the row containing filter options like mesh, skeleton, materials
    // Each of of these categorie will be split for better visibility, the buttons here will allow (dis/en)abling these
    void renderFilterRow();
    // inside of the renderFilterRow, a searchbar will be rendered for searching for specific options more directly
    void renderSearchbar();

    void renderFooter();

  private:
    const Rapture::AssetType assetType;
};

#endif // RP_EDITOR__IMPORT_ASSET_PANEL_H

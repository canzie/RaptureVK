#ifndef RAPTURE__ASSET_WORKSPACE_H
#define RAPTURE__ASSET_WORKSPACE_H

#include "Workspace.h"

#include <rapture/assets/asset_manager/AssetCommon.h>

/**
 * @brief A workspace that exists for one asset, taken as it is opened.
 *
 * TODO: close the workspace when its asset is deleted, which needs a signal the asset manager
 * does not raise yet.
 */
class AssetWorkspace : public Workspace {
  public:
    /**
     * @brief Creates a workspace for one asset
     * @param context What the whole editor reaches
     * @param handle The asset this workspace is for
     */
    AssetWorkspace(const EditorContext &context, Rapture::AssetHandle handle);

    Rapture::AssetHandle handle() const { return m_handle; }

    /**
     * @brief The name the open asset goes by, for the tab and the panels
     */
    std::string_view assetName() const;

    std::string_view tabLabel() const override { return assetName(); }

    /**
     * @brief The color of the open asset's type
     * @return The color
     */
    std::optional<Amethyst::Color4> tabIconColor() const override;

  protected:
    /**
     * @brief The stored name, which carries the asset so two workspaces of one kind stay apart
     */
    std::string dockLayoutName() const override;

    /**
     * @brief Writes the open asset back to its asset file
     */
    void saveOpenAsset();

  protected:
    Rapture::AssetHandle m_handle = Rapture::INVALID_ASSET_HANDLE;
};

#endif // RAPTURE__ASSET_WORKSPACE_H

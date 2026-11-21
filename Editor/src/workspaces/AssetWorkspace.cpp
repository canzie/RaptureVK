#include "AssetWorkspace.h"

#include "ui/AssetVisuals.h"

#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/core/utils/Log.h>

AssetWorkspace::AssetWorkspace(const EditorContext &context, Rapture::AssetHandle handle) : Workspace(context), m_handle(handle) {}

std::string_view AssetWorkspace::assetName() const
{
    return Rapture::AssetManager::getAssetMetadata(m_handle).getName();
}

std::string AssetWorkspace::dockLayoutName() const
{
    return std::string(assetName());
}

std::optional<Amethyst::Color4> AssetWorkspace::tabIconColor() const
{
    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(m_handle);
    return Amethyst::Color4(Asset_colorForType(metadata.assetType), 1.0f);
}

void AssetWorkspace::saveOpenAsset()
{
    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(m_handle);
    if (!Rapture::AssetManager::saveAsset(m_handle, metadata.assetPath.parent_path())) {
        RP_ERROR("could not save '{}'", metadata.getName());
    }
}

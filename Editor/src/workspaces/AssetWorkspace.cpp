#include "AssetWorkspace.h"

#include <rapture/assets/asset_manager/AssetManager.h>

AssetWorkspace::AssetWorkspace(const EditorContext &context, Rapture::AssetHandle handle) : Workspace(context), m_handle(handle) {}

std::string_view AssetWorkspace::assetName() const
{
    return Rapture::AssetManager::getAssetMetadata(m_handle).getName();
}

std::string AssetWorkspace::dockLayoutName() const
{
    return std::string(assetName());
}

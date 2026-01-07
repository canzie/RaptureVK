#pragma once

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <utility>

#include "Asset.h"
#include "AssetManagerBase.h"

namespace Rapture {

class AssetManagerEditor : public AssetManagerBase {
  public:
    AssetManagerEditor();
    ~AssetManagerEditor();

    bool isAssetLoaded(AssetHandle handle) const;
    virtual bool isAssetHandleValid(AssetHandle handle) const override;
    virtual Asset &getAsset(AssetHandle handle) override;

    AssetMetadata &getAssetMetadata(AssetHandle handle);
    const AssetMetadata &getAssetMetadata(AssetHandle handle) const;

    Asset &importAsset(std::filesystem::path path, AssetImportConfigVariant importConfig = std::monostate());

    Asset &importDefaultAsset(AssetType assetType);

    Asset &registerVirtualAsset(AssetVariant asset, const std::string &virtualName, AssetType assetType);
    bool unregisterVirtualAsset(AssetHandle handle);

    Asset &getVirtualAssetByName(const std::string &virtualName);
    std::vector<AssetHandle> getVirtualAssetsByType(AssetType type) const;

  private:
    // Determine asset type from file extension
    AssetType determineAssetType(const std::string &path);

    std::unordered_map<AssetType, AssetHandle> m_defaultAssetHandles;
};

} // namespace Rapture

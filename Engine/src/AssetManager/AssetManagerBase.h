#pragma once

#include "Asset.h"
#include "AssetImporter.h"

#include <memory>
#include <unordered_map>

namespace Rapture {

using AssetMap = std::unordered_map<AssetHandle, std::unique_ptr<Asset>>;
using AssetRegistry = std::unordered_map<AssetHandle, AssetMetadata>;

/*
 @brief Base class for all asset manager implementations

 intented for the Editor Asset Manager and the Runtime Asset Manager (for actual games)
*/
class AssetManagerBase {
  public:
    AssetManagerBase() { AssetImporter::init(); };
    virtual ~AssetManagerBase() { AssetImporter::shutdown(); };

    virtual bool isAssetHandleValid(AssetHandle handle) const = 0;
    virtual const Asset &getAsset(AssetHandle handle) = 0;

    const AssetRegistry &getAssetRegistry() const { return m_assetRegistry; }
    const AssetMap &getLoadedAssets() const { return m_loadedAssets; }

  protected:
    AssetMap m_loadedAssets;
    AssetRegistry m_assetRegistry;
};

} // namespace Rapture

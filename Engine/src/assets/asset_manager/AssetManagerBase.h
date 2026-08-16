#ifndef RAPTURE__ASSET_MANAGER_BASE_H
#define RAPTURE__ASSET_MANAGER_BASE_H

#include "Asset.h"
#include "AssetImporter.h"
#include "AssetStorage.h"

namespace Rapture {

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

    const AssetStorage &getAssets() const { return m_assets; }

  protected:
    AssetStorage m_assets;
};

} // namespace Rapture

#endif // RAPTURE__ASSET_MANAGER_BASE_H

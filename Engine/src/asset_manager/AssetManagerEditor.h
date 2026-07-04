#pragma once

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

#include "concurrentqueue.h"

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

    void onUpdate();

    /**
     * @brief Queues an asset for an eviction check, safe to call from any thread
     * @param handle The asset to check for unloading
     */
    void requestUnload(AssetHandle handle);

  private:
    // Determine asset type from file extension
    AssetType determineAssetType(const std::string &path);

    bool evictAsset(AssetHandle handle);
    void ensureDeferredFreeBuckets();

    std::unordered_map<AssetType, AssetHandle> m_defaultAssetHandles;

    // One bucket is freed per frame as we rotate through them, so an evicted asset outlives every in-flight frame
    std::vector<std::vector<std::unique_ptr<Asset>>> m_deferredFrees;
    size_t m_deferredFreeBucket = 0;
    bool m_shuttingDown = false;

    moodycamel::ConcurrentQueue<AssetHandle> m_pendingUnloadChecks;
};

} // namespace Rapture

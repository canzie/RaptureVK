#ifndef RAPTURE__ASSET_MANAGER_EDITOR_H
#define RAPTURE__ASSET_MANAGER_EDITOR_H

#include <filesystem>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "concurrentqueue.h"

#include "Asset.h"
#include "AssetManagerBase.h"
#include "utils/PriorityQueue.h"

namespace Rapture {

struct Telemetry;

class AssetManagerEditor : public AssetManagerBase {
  public:
    explicit AssetManagerEditor(const Telemetry *telemetry);
    ~AssetManagerEditor();

    bool isAssetLoaded(AssetHandle handle) const;
    virtual bool isAssetHandleValid(AssetHandle handle) const override;
    virtual Asset &getAsset(AssetHandle handle) override;

    AssetMetadata &getAssetMetadata(AssetHandle handle);
    const AssetMetadata &getAssetMetadata(AssetHandle handle) const;

    /**
     * @brief Imports an external non-.rasset file into an owned asset
     * @param request The source file, output folder, import config and name
     * @return The imported asset, or Asset::null on failure
     */
    Asset &importAsset(const AssetImportFileRequest &request);

    /**
     * @brief Imports in-memory data into an owned asset, writing its .rasset
     * @param request The data, output folder, name and provenance
     * @return The imported asset, or Asset::null on failure
     */
    Asset &importAsset(AssetImportDataRequest request);

    /**
     * @brief Registers an owned asset from its .rasset file without loading its data
     * @param path The .rasset file
     * @return The registered asset handle, or INVALID_ASSET_HANDLE on failure
     */
    AssetHandle registerRaptureAsset(std::filesystem::path path);

    /**
     * @brief Registers every .rasset under a directory tree without loading their data
     * @param directory The directory to scan recursively
     * @return The number of newly registered assets
     */
    uint32_t registerAssetDirectory(const std::filesystem::path &directory);

    /**
     * @brief Replaces an owned asset's contents and rewrites its .rasset in place
     * @param handle The asset to overwrite, which must already be registered from a file
     * @param asset The new asset value
     * @return True if the file now holds the new contents
     */
    bool updateAsset(AssetHandle handle, AssetVariant asset);

    /**
     * @brief Writes a loaded asset back into its file, giving it one if it has none yet
     * @param handle The asset to write
     * @param folder Directory an asset with no file yet is written into
     * @return True if the asset's file now holds its contents
     */
    bool saveAsset(AssetHandle handle, const std::filesystem::path &folder);

    Asset &importDefaultAsset(AssetType assetType);

    Asset &registerVirtualAsset(AssetVariant asset, const std::string &virtualName, AssetType assetType);
    bool unregisterVirtualAsset(AssetHandle handle);

    /**
     * @brief Registers an engine builtin under its fixed reserved handle, recreated in code each run
     * @param handle The reserved handle
     * @param asset The built asset value
     * @param name A display name for the asset
     * @param assetType The asset type
     * @return The registered asset, or the existing one if the handle is already registered
     */
    Asset &registerReservedAsset(AssetHandle handle, AssetVariant asset, const std::string &name, AssetType assetType);

    /**
     * @brief Builds every engine builtin, so a scene referencing one resolves it whatever the load order
     *
     * Runs once the buffer arenas and the material manager exist, which the builtins allocate from.
     */
    void registerBuiltinAssets();

    Asset &getVirtualAssetByName(const std::string &virtualName);
    std::vector<AssetHandle> getVirtualAssetsByType(AssetType type) const;

    /**
     * @brief Looks up the asset registered at an on-disk .rasset path
     * @param path The .rasset file path
     * @return The asset handle, or INVALID_ASSET_HANDLE if no asset is registered there
     */
    AssetHandle findAssetByPath(const std::filesystem::path &path) const;

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

    void processUnloadRequests();
    void addToColdList(AssetHandle handle, const AssetMetadata &metadata);
    void drainColdList();

    /**
     * @brief Writes the .rasset for any imported asset whose load has finished
     */
    void processPendingWrites();

    /**
     * @brief Writes an asset's payload to <folder>/<name>.rasset and records its path
     * @param handle The asset handle stored in the file
     * @param folder The output directory
     * @param metadata The asset metadata encoded into the file
     * @param payload The serialized asset bytes
     */
    void writeRaptureAssetFile(AssetHandle handle, const std::filesystem::path &folder, AssetMetadata &metadata,
                               std::span<const uint8_t> payload);

    /**
     * @brief Registers a built asset, then writes its .rasset now or defers it until its load finishes
     * @param handle The asset handle
     * @param asset The built asset, loaded or still loading
     * @param metadata The asset metadata
     * @param outputFolder Directory the owned .rasset is written into, empty to skip
     * @param payload The serialized bytes for a synchronous write, empty to defer to the async load
     * @return The registered asset
     */
    Asset &registerImportedAsset(AssetHandle handle, std::unique_ptr<Asset> asset, std::unique_ptr<AssetMetadata> metadata,
                                 const std::filesystem::path &outputFolder, std::span<const uint8_t> payload);

    /**
     * @brief Rebuilds an asset from its reload source described in the metadata
     * @param handle The asset handle
     * @param metadata The asset's metadata
     * @return The rebuilt asset, or nullptr on failure
     */
    std::unique_ptr<Asset> loadFromMetadata(AssetHandle handle, AssetMetadata &metadata);

    /**
     * @brief An import whose async load must finish before its .rasset can be written
     *
     * The ref pins the asset for the lifetime of the entry, so an eviction cannot destroy it before its payload is serialized.
     */
    struct PendingWrite {
        AssetRef asset;
        std::filesystem::path outputFolder;
    };

    const Telemetry *m_telemetry = nullptr;

    std::unordered_map<AssetType, AssetHandle> m_defaultAssetHandles;

    // One bucket is freed per frame as we rotate through them, so an evicted asset outlives every in-flight frame
    std::vector<std::vector<std::unique_ptr<Asset>>> m_deferredFrees;
    size_t m_deferredFreeBucket = 0;
    bool m_shuttingDown = false;

    moodycamel::ConcurrentQueue<AssetHandle> m_pendingUnloadChecks;

    PriorityQueue<AssetHandle> m_coldList;
    bool m_coldDraining = false;

    // Hash of a .rasset path to the handle registered at that path
    std::unordered_map<uint64_t, AssetHandle> m_pathIndex;

    std::vector<PendingWrite> m_pendingWrites;
};

} // namespace Rapture

#endif // RAPTURE__ASSET_MANAGER_EDITOR_H

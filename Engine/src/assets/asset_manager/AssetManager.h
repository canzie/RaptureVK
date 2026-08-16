#ifndef RAPTURE__ASSET_MANAGER_H
#define RAPTURE__ASSET_MANAGER_H

#include "AssetManagerEditor.h"
#include "AssetStorage.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Asset.h"
#include "core/events/ProjectEvents.h"
#include "core/utils/UUID.h"

#include "core/utils/Log.h"
#include "core/utils/rp_assert.h"

namespace Rapture {

struct Telemetry;

class AssetManager {
  public:
    static void init(const Telemetry *telemetry)
    {
        if (s_isInitialized) {
            RP_CORE_WARN("AssetManager already initialized");
            return;
        }
        s_activeAssetManager = new AssetManagerEditor(telemetry);
        s_isInitialized = true;

        s_serializeListener = ProjectEvents::onProjectSerialize().addListener([](WriteNode &root) { (void)root; });
        s_registerListener = ProjectEvents::onProjectRegister().addListener([](ReadNode &root) { (void)root; });
    }

    static void shutdown();

    static AssetRef getAsset(AssetHandle handle)
    {
        Asset &asset = s_activeAssetManager->getAsset(handle);
        if (!asset) {
            return AssetRef();
        }
        AssetMetadata &metadata = s_activeAssetManager->getAssetMetadata(handle);

        return AssetRef(&asset, &metadata.useCount);
    }

    static AssetRef importAsset(const AssetImportFileRequest &request)
    {
        auto &asset = s_activeAssetManager->importAsset(request);

        if (!asset || !asset.isValid()) {
            return AssetRef();
        }

        AssetMetadata &metadata = s_activeAssetManager->getAssetMetadata(asset.getHandle());
        return AssetRef(&asset, &metadata.useCount);
    }

    static AssetRef importAsset(std::filesystem::path path, AssetImportConfigVariant importConfig = std::monostate(),
                                std::string name = {})
    {
        return importAsset(
            AssetImportFileRequest{.source = std::move(path), .config = std::move(importConfig), .name = std::move(name)});
    }

    static AssetRef importAsset(AssetImportDataRequest request)
    {
        auto &asset = s_activeAssetManager->importAsset(std::move(request));

        if (!asset || !asset.isValid()) {
            return AssetRef();
        }

        AssetMetadata &metadata = s_activeAssetManager->getAssetMetadata(asset.getHandle());
        return AssetRef(&asset, &metadata.useCount);
    }

    static bool updateAsset(AssetHandle handle, AssetVariant asset)
    {
        return s_activeAssetManager->updateAsset(handle, std::move(asset));
    }

    static bool saveAsset(AssetHandle handle, const std::filesystem::path &folder)
    {
        return s_activeAssetManager->saveAsset(handle, folder);
    }

    static AssetHandle registerRaptureAsset(std::filesystem::path path)
    {
        return s_activeAssetManager->registerRaptureAsset(std::move(path));
    }

    static uint32_t registerAssetDirectory(const std::filesystem::path &directory)
    {
        return s_activeAssetManager->registerAssetDirectory(directory);
    }

    static AssetHandle findAssetByPath(const std::filesystem::path &path)
    {
        return s_activeAssetManager->findAssetByPath(path);
    }

    static AssetRef importDefaultAsset(AssetType assetType)
    {
        auto &asset = s_activeAssetManager->importDefaultAsset(assetType);
        if (!asset) {
            return AssetRef();
        }

        AssetMetadata &metadata = s_activeAssetManager->getAssetMetadata(asset.getHandle());
        return AssetRef(&asset, &metadata.useCount);
    }

    static AssetRef registerVirtualAsset(AssetVariant &&assetValue, const std::string &virtualName, AssetType assetType)
    {
        if (!s_isInitialized || !s_activeAssetManager) {
            RP_CORE_ERROR("AssetManager not initialized");
            return AssetRef();
        }
        auto &asset = s_activeAssetManager->registerVirtualAsset(std::move(assetValue), virtualName, assetType);
        auto &metdata = s_activeAssetManager->getAssetMetadata(asset.getHandle());
        return asset ? AssetRef(&asset, &metdata.useCount) : AssetRef();
    }

    static AssetRef registerReservedAsset(AssetHandle handle, AssetVariant &&assetValue, const std::string &name,
                                          AssetType assetType)
    {
        if (!s_isInitialized || !s_activeAssetManager) {
            RP_CORE_ERROR("AssetManager not initialized");
            return AssetRef();
        }
        auto &asset = s_activeAssetManager->registerReservedAsset(handle, std::move(assetValue), name, assetType);
        auto &metadata = s_activeAssetManager->getAssetMetadata(asset.getHandle());
        return asset ? AssetRef(&asset, &metadata.useCount) : AssetRef();
    }

    static void registerBuiltinAssets()
    {
        if (!s_isInitialized || !s_activeAssetManager) {
            RP_CORE_ERROR("AssetManager not initialized");
            return;
        }
        s_activeAssetManager->registerBuiltinAssets();
    }

    static bool unregisterVirtualAsset(AssetHandle handle)
    {
        if (!s_isInitialized || !s_activeAssetManager) {
            RP_CORE_ERROR("AssetManager not initialized");
            return false;
        }
        return s_activeAssetManager->unregisterVirtualAsset(handle);
    }

    static AssetRef getVirtualAsset(const std::string &virtualName)
    {
        if (!s_isInitialized || !s_activeAssetManager) {
            RP_CORE_ERROR("AssetManager not initialized");
            return AssetRef();
        }
        Asset &asset = s_activeAssetManager->getVirtualAssetByName(virtualName);
        if (!asset || !asset.isValid()) {
            return AssetRef();
        }

        AssetMetadata &metadata = s_activeAssetManager->getAssetMetadata(asset.getHandle());
        return AssetRef(&asset, &metadata.useCount);
    }

    static std::vector<AssetHandle> getVirtualAssetsByType(AssetType type)
    {
        if (!s_isInitialized || !s_activeAssetManager) {
            RP_CORE_ERROR("AssetManager not initialized");
            return {};
        }
        return s_activeAssetManager->getVirtualAssetsByType(type);
    }

    /**
     * @brief The live assets of one type
     * @param type The type to walk
     * @return A range over that type's slots
     */
    static AssetTypeView getAssetsOfType(AssetType type)
    {
        if (!s_isInitialized || s_activeAssetManager == nullptr) {
            RP_CORE_ERROR("AssetManager not initialized");
            return AssetTypeView({}, 0);
        }
        return s_activeAssetManager->getAssets().ofType(type);
    }

    /**
     * @brief Collects the handles of every live asset of one type
     * @param type The type to collect
     * @return The handles, in slot order
     */
    static std::vector<AssetHandle> getHandlesOfType(AssetType type)
    {
        if (!s_isInitialized || s_activeAssetManager == nullptr) {
            RP_CORE_ERROR("AssetManager not initialized");
            return {};
        }
        return s_activeAssetManager->getAssets().handlesOfType(type);
    }

    static AssetMetadata &getAssetMetadata(AssetHandle handle)
    {
        if (!s_isInitialized || !s_activeAssetManager) {
            RP_CORE_ERROR("AssetManager not initialized");
            return AssetMetadata::null;
        }
        return s_activeAssetManager->getAssetMetadata(handle);
    }

    static void onUpdate()
    {
        RP_ASSERT(s_isInitialized && s_activeAssetManager != nullptr, "AssetManager not initialized");
        s_activeAssetManager->onUpdate();
    }

    static void requestUnload(AssetHandle handle)
    {
        RP_ASSERT(s_isInitialized && s_activeAssetManager != nullptr, "AssetManager not initialized");
        s_activeAssetManager->requestUnload(handle);
    }

  private:
    static bool s_isInitialized;
    static AssetManagerEditor *s_activeAssetManager;
    static EventListenerId s_serializeListener;
    static EventListenerId s_registerListener;
};

} // namespace Rapture

#endif // RAPTURE__ASSET_MANAGER_H

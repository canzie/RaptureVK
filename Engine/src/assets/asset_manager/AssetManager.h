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

    static AssetRef getAsset(AssetHandle handle) { return AssetRef(s_activeAssetManager->getAsset(handle)); }

    /**
     * @brief Takes a use of an asset of a known class
     * @param handle The asset to hold
     * @return A use of the asset as a T, empty if it is not registered or is not a T
     */
    template <typename T>
    static Ref<T> getAsset(AssetHandle handle)
    {
        Asset *asset = s_activeAssetManager->getAsset(handle);
        return Ref<T>(asset != nullptr ? asset->as<T>() : nullptr);
    }

    static AssetRef importAsset(const AssetImportFileRequest &request)
    {
        Asset *asset = s_activeAssetManager->importAsset(request);

        if (asset == nullptr || !asset->isValid()) {
            return AssetRef();
        }

        return AssetRef(asset);
    }

    static AssetRef importAsset(std::filesystem::path path, AssetImportConfigVariant importConfig = std::monostate(),
                                std::string name = {})
    {
        return importAsset(
            AssetImportFileRequest{.source = std::move(path), .config = std::move(importConfig), .name = std::move(name)});
    }

    /**
     * @brief Imports a file whose class is known, so the caller never handles it untyped
     * @param path The source file
     * @param importConfig How the source is read
     * @param name A name for the asset, defaulting to the file's stem
     * @return A use of the imported asset as a T, empty if it could not be imported or is not a T
     */
    template <typename T>
    static Ref<T> importAsset(std::filesystem::path path, AssetImportConfigVariant importConfig = std::monostate(),
                              std::string name = {})
    {
        return importAsset(std::move(path), std::move(importConfig), std::move(name)).template as<T>();
    }

    /**
     * @brief Imports in-memory data whose class is known, so the caller never handles it untyped
     * @param request The data, output folder, name and provenance
     * @return A use of the imported asset as a T, empty if it could not be imported or is not a T
     */
    template <typename T>
    static Ref<T> importAsset(AssetImportDataRequest request)
    {
        return importAsset(std::move(request)).template as<T>();
    }

    static AssetRef importAsset(AssetImportDataRequest request)
    {
        Asset *asset = s_activeAssetManager->importAsset(std::move(request));

        if (asset == nullptr || !asset->isValid()) {
            return AssetRef();
        }

        return AssetRef(asset);
    }

    static bool updateAsset(AssetHandle handle, std::unique_ptr<Asset> asset)
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

    static AssetHandle findAssetByPath(const std::filesystem::path &path) { return s_activeAssetManager->findAssetByPath(path); }

    static AssetRef importDefaultAsset(AssetType assetType)
    {
        return AssetRef(s_activeAssetManager->importDefaultAsset(assetType));
    }

    static AssetRef registerVirtualAsset(std::unique_ptr<Asset> assetValue, const std::string &virtualName, AssetType assetType)
    {
        if (!s_isInitialized || !s_activeAssetManager) {
            RP_CORE_ERROR("AssetManager not initialized");
            return AssetRef();
        }
        return AssetRef(s_activeAssetManager->registerVirtualAsset(std::move(assetValue), virtualName, assetType));
    }

    static AssetRef registerReservedAsset(AssetHandle handle, std::unique_ptr<Asset> assetValue, const std::string &name,
                                          AssetType assetType)
    {
        if (!s_isInitialized || !s_activeAssetManager) {
            RP_CORE_ERROR("AssetManager not initialized");
            return AssetRef();
        }
        return AssetRef(s_activeAssetManager->registerReservedAsset(handle, std::move(assetValue), name, assetType));
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
        Asset *asset = s_activeAssetManager->getVirtualAssetByName(virtualName);
        if (asset == nullptr || !asset->isValid()) {
            return AssetRef();
        }

        return AssetRef(asset);
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

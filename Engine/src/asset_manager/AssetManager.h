#pragma once

#include "AssetManagerEditor.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Asset.h"
#include "events/ProjectEvents.h"
#include "utils/UUID.h"

#include "logging/Log.h"
#include "utils/rp_assert.h"

namespace Rapture {

class AssetManager {
  public:
    static void init()
    {
        if (s_isInitialized) {
            RP_CORE_WARN("AssetManager already initialized");
            return;
        }
        s_activeAssetManager = new AssetManagerEditor();
        s_isInitialized = true;

        s_serializeListener = ProjectEvents::onProjectSerialize().addListener([](WriteNode &root) { (void)root; });
        s_registerListener = ProjectEvents::onProjectRegister().addListener([](ReadNode &root) { (void)root; });
    }

    static void shutdown();

    static AssetRef getAsset(AssetHandle handle)
    {
        Asset &asset = s_activeAssetManager->getAsset(handle);
        AssetMetadata &metadata = s_activeAssetManager->getAssetMetadata(handle);

        return AssetRef(&asset, &metadata.useCount);
    }

    static AssetRef importAsset(std::filesystem::path path, AssetImportConfigVariant importConfig = std::monostate())
    {
        auto &asset = s_activeAssetManager->importAsset(path, importConfig);

        if (!asset || !asset.isValid()) {
            return AssetRef();
        }

        AssetMetadata &metadata = s_activeAssetManager->getAssetMetadata(asset.getHandle());
        return AssetRef(&asset, &metadata.useCount);
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

    static const AssetRegistry &getAssetRegistry()
    {
        if (!s_isInitialized || !s_activeAssetManager) {
            RP_CORE_ERROR("AssetManager not initialized");
            static AssetRegistry emptyRegistry;
            return emptyRegistry;
        }
        return s_activeAssetManager->getAssetRegistry();
    }

    static const AssetMap &getLoadedAssets()
    {
        if (!s_isInitialized || !s_activeAssetManager) {
            RP_CORE_ERROR("AssetManager not initialized");
            static AssetMap emptyMap;
            return emptyMap;
        }
        return s_activeAssetManager->getLoadedAssets();
    }

    static std::vector<AssetHandle> getTextures()
    {
        if (!s_isInitialized || !s_activeAssetManager) {
            RP_CORE_ERROR("AssetManager not initialized");
            return {};
        }

        std::vector<AssetHandle> textures;
        for (const auto &[handle, metadata] : s_activeAssetManager->getAssetRegistry()) {
            if (metadata->assetType == AssetType::TEXTURE) {
                textures.push_back(handle);
            }
        }

        return textures;
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

#pragma once

#include "AssetManagerEditor.h"


#include <string>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include <vector>

#include "Asset.h"
#include "Utils/UUID.h"

#include "Logging/Log.h"


namespace Rapture {



    class AssetManager
    {
    public:

        static void init() {
            if (s_isInitialized) {
                RP_CORE_WARN("AssetManager already initialized");
                return;
            }
            s_activeAssetManager = new AssetManagerEditor();
            s_isInitialized = true;
        }

        static void shutdown() {
            if (!s_isInitialized) {
                RP_CORE_WARN("AssetManager not initialized");
                return;
            }
            delete s_activeAssetManager;
            s_isInitialized = false;
        }

        template<typename T>
        static std::shared_ptr<T> getAsset(AssetHandle handle) {
            // get the active asset manager from the "project"
            std::shared_ptr<Asset> asset = s_activeAssetManager->getAsset(handle);
            return asset->getUnderlyingAsset<T>();
        }

        template<typename T>
        static std::pair<std::shared_ptr<T>, AssetHandle> importAsset(std::filesystem::path path, std::vector<uint32_t> indices = {0}, AssetType assetType = AssetType::None) {
            auto [asset, handle] = s_activeAssetManager->importAsset(path, indices, assetType);
            return std::make_pair(asset->getUnderlyingAsset<T>(), handle);
        }



        // Helper method for UI to access the asset registry
        static const AssetRegistry& getAssetRegistry() {
            if (!s_isInitialized || !s_activeAssetManager) {
                RP_CORE_ERROR("AssetManager not initialized");
                static AssetRegistry emptyRegistry;
                return emptyRegistry;
            }
            return s_activeAssetManager->getAssetRegistry();
        }


        static const AssetMap& getLoadedAssets() {
            if (!s_isInitialized || !s_activeAssetManager) {
                RP_CORE_ERROR("AssetManager not initialized");
                static AssetMap emptyMap;
                return emptyMap;
            }
            return s_activeAssetManager->getLoadedAssets();
        }
    private:
        static bool s_isInitialized;
        static AssetManagerEditor* s_activeAssetManager;

        
        
    };


}


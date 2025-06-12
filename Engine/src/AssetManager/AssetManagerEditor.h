#pragma once

#include <unordered_map>
#include <memory>
#include <utility>
#include <filesystem>

#include "AssetManagerBase.h"
#include "Asset.h"

namespace Rapture {

    using AssetRegistry = std::unordered_map<AssetHandle, AssetMetadata>;

    class AssetManagerEditor : public AssetManagerBase {
        public:
            AssetManagerEditor();
            ~AssetManagerEditor();


            bool isAssetLoaded(AssetHandle handle) const;
            virtual bool isAssetHandleValid(AssetHandle handle) const override;
            virtual std::shared_ptr<Asset> getAsset(AssetHandle handle) override;


            // Get metadata for an asset
            const AssetMetadata& getAssetMetadata(AssetHandle handle) const;

            std::pair<std::shared_ptr<Asset>, AssetHandle> importAsset(std::filesystem::path path, AssetImportConfigVariant importConfig = std::monostate());

            std::pair<std::shared_ptr<Asset>, AssetHandle> importDefaultAsset(AssetType assetType);

            
            AssetHandle registerVirtualAsset(
                std::shared_ptr<AssetVariant> asset,
                const std::string& virtualName, 
                AssetType assetType
            );
            
            bool unregisterVirtualAsset(AssetHandle handle);
            
            // Virtual asset query methods
            AssetHandle getVirtualAssetByName(const std::string& virtualName) const;
            std::vector<AssetHandle> getVirtualAssetsByType(AssetType type) const;

            const AssetRegistry& getAssetRegistry() const { return m_assetRegistry; }
            const AssetMap& getLoadedAssets() const { return m_loadedAssets; }
            

        private:
            // Determine asset type from file extension
            AssetType determineAssetType(const std::string& path);
            
            AssetRegistry m_assetRegistry;
            
            // Track default asset handles to avoid recreating them
            std::unordered_map<AssetType, AssetHandle> m_defaultAssetHandles;
    };



}

// asset manager
// - get assets
// - load assets


// flow:
// User imports an asset
// goes trough the asset manager
// if the loader needs more assets lke images or materials -> uses the asset manager to load them


// when an asset is loaded,
// 1. it will be assigned a unique handle (old one or new, depending if metadata exists and is valid)
// 2. it will be added to the loaded assets map as an asset
// 3. metadata will be added to the registry


// when an asset is requested, check the metadata, if it is there, we have loaded it before
// -> check the loaded assets map with entity handle, return if loaded already
// -> if not loaded, load it

// => find best datastructure to store the metadata, need fast lookup for strings


// MAIN ISSUE NOW : what do the components store?, the asset itself, the handle, a reference to the asset?
// - if the asset handle is stored -> overhead when retrieving the underlying asset each time
// - if the asset is stored -> less overhead but still more than the underlying asset itself
// - if a reference to the asset is stored -> less to no overhead, but, hard to serialize and link to the assets metadata, like filepath


// SOLUTION(s)? : 
// - store the asset handle in the component with the underlying asset pointer -> (only possible if the asset handle cant be changed, so we can maybe store an asset pointer??)



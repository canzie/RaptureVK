#pragma once

#include "Asset.h"

#include <memory>
#include <map>
#include <functional>
#include <variant>

#include "Logging/Log.h"

namespace Rapture {

    using AssetImporterFunction = std::function<std::shared_ptr<Asset>(AssetHandle, const AssetMetadata&)>;
    static std::map<AssetType, AssetImporterFunction> s_assetImporters;

    class AssetImporter {
    
    public:

        static void init(){
            if (s_isInitialized) {
                RP_CORE_WARN("AssetImporter already initialized");
                return;
            }
            s_assetImporters[AssetType::Shader] = loadShader;
            s_assetImporters[AssetType::Material] = loadMaterial;
            s_isInitialized = true;
        }
        static void shutdown(){
            if (!s_isInitialized) {
                RP_CORE_WARN("AssetImporter not initialized");
                return;
            }
            s_assetImporters.clear();
            s_isInitialized = false;
        }

        static std::shared_ptr<Asset> importAsset(const AssetHandle& handle, const AssetMetadata& metadata){
            
            return s_assetImporters[metadata.m_assetType](handle, metadata);
        }

    private:


        static std::shared_ptr<Asset> loadShader(const AssetHandle& handle, const AssetMetadata& metadata);

        static std::shared_ptr<Asset> loadMaterial(const AssetHandle& handle, const AssetMetadata& metadata);



    private:
        static bool s_isInitialized;
        
        
        
        

    };

}


// for loading meshes, we can load each primitive as an asset if loaded trough a non-static mesh
// -> need a way to save multiple meshes belonging to the same gltf file, same for the materials
// 
// what is a model asset?


#pragma once

#include "Asset.h"
#include "AssetImporter.h"

#include <unordered_map>
#include <string>
#include <memory>

namespace Rapture {

    using AssetMap = std::unordered_map<AssetHandle, std::shared_ptr<Asset>>;

    class AssetManagerBase {
        public:
            AssetManagerBase(){
                AssetImporter::init(4);
            };
            virtual ~AssetManagerBase(){
                AssetImporter::shutdown();
            };

            virtual bool isAssetHandleValid(AssetHandle handle) const = 0;
            virtual std::shared_ptr<Asset> getAsset(AssetHandle handle) = 0;
            
        protected:
            AssetMap m_loadedAssets;
            
    };

}

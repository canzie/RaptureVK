#ifndef RAPTURE__ASSET_IMPORTER_H
#define RAPTURE__ASSET_IMPORTER_H

#include "Asset.h"

#include <functional>
#include <map>

#include "Logging/Log.h"

namespace Rapture {

using AssetImporterFunction = std::function<bool(Asset &, AssetMetadata &)>;
static std::map<AssetType, AssetImporterFunction> s_assetImporters;

class AssetImporter {

  public:
    static void init()
    {
        if (s_isInitialized) {
            RP_CORE_WARN("AssetImporter already initialized");
            return;
        }
        s_assetImporters[AssetType::SHADER] = loadShader;
        s_assetImporters[AssetType::MATERIAL] = loadMaterial;
        s_assetImporters[AssetType::TEXTURE] = loadTexture;
        s_assetImporters[AssetType::CUBEMAP] = loadCubemap;
        s_assetImporters[AssetType::SCENE] = loadScene;
        s_isInitialized = true;
    }

    static void shutdown()
    {
        if (!s_isInitialized) {
            RP_CORE_WARN("AssetImporter not initialized");
            return;
        }

        s_assetImporters.clear();
        s_isInitialized = false;
    }

    static bool importAsset(Asset &asset, AssetMetadata &metadata) { return s_assetImporters[metadata.assetType](asset, metadata); }

  private:
    static bool loadShader(Asset &asset, AssetMetadata &metadata);
    static bool loadMaterial(Asset &asset, AssetMetadata &metadata);
    static bool loadTexture(Asset &asset, AssetMetadata &metadata);
    static bool loadCubemap(Asset &asset, AssetMetadata &metadata);
    static bool loadScene(Asset &asset, AssetMetadata &metadata);

  private:
    static bool s_isInitialized;
};

} // namespace Rapture

#endif // RAPTURE__ASSET_IMPORTER_H

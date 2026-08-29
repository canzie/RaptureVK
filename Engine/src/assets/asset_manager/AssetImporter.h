#ifndef RAPTURE__ASSET_IMPORTER_H
#define RAPTURE__ASSET_IMPORTER_H

#include "Asset.h"

#include <functional>
#include <map>
#include <memory>

#include "core/utils/Log.h"

namespace Rapture {

using AssetImporterFunction = std::function<std::unique_ptr<Asset>(AssetMetadata &, AssetHandle)>;
static std::map<AssetType, AssetImporterFunction> s_assetImporters;

class AssetImporter {

  public:
    static void init()
    {
        if (s_isInitialized) {
            RP_CORE_WARN("AssetImporter already initialized");
            return;
        }
        s_assetImporters[ASSET_SHADER] = loadShader;
        s_assetImporters[ASSET_MATERIAL_INSTANCE] = loadMaterial;
        s_assetImporters[ASSET_TEXTURE] = loadTexture;
        s_assetImporters[ASSET_CUBEMAP] = loadCubemap;
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

    /**
     * @brief Builds an asset from the external file its metadata names
     * @param metadata The asset's metadata, whose assetType picks the importer
     * @param handle The handle the asset is registered under, set before any async load can report against it
     * @return The imported asset, or nullptr if its type has no importer or the source could not be read
     */
    static std::unique_ptr<Asset> importAsset(AssetMetadata &metadata, AssetHandle handle)
    {
        auto it = s_assetImporters.find(metadata.assetType);
        if (it == s_assetImporters.end()) {
            RP_CORE_ERROR("asset type {} has no importer", static_cast<int>(metadata.assetType));
            return nullptr;
        }

        return it->second(metadata, handle);
    }

  private:
    static std::unique_ptr<Asset> loadShader(AssetMetadata &metadata, AssetHandle handle);
    static std::unique_ptr<Asset> loadMaterial(AssetMetadata &metadata, AssetHandle handle);
    static std::unique_ptr<Asset> loadTexture(AssetMetadata &metadata, AssetHandle handle);
    static std::unique_ptr<Asset> loadCubemap(AssetMetadata &metadata, AssetHandle handle);

  private:
    static bool s_isInitialized;
};

} // namespace Rapture

#endif // RAPTURE__ASSET_IMPORTER_H

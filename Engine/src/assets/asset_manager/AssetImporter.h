#ifndef RAPTURE__ASSET_IMPORTER_H
#define RAPTURE__ASSET_IMPORTER_H

#include "Asset.h"

#include <memory>

namespace Rapture {

/**
 * @brief Builds a shader from the external file its metadata names
 * @param metadata The asset's metadata, naming the source to compile
 * @param handle The handle the asset is registered under, set before any async load reports against it
 * @return The imported asset, or nullptr if the source could not be read
 */
std::unique_ptr<Asset> Asset_importShader(AssetMetadata &metadata, AssetHandle handle);

/**
 * @brief Builds a material instance from the external file its metadata names
 * @param metadata The asset's metadata, naming the source to read
 * @param handle The handle the asset is registered under, set before any async load reports against it
 * @return The imported asset, or nullptr if the source could not be read
 */
std::unique_ptr<Asset> Asset_importMaterialInstance(AssetMetadata &metadata, AssetHandle handle);

/**
 * @brief Builds a texture from the external image its metadata names
 * @param metadata The asset's metadata, naming the image to decode
 * @param handle The handle the asset is registered under, set before any async load reports against it
 * @return The imported asset, or nullptr if the image could not be read
 */
std::unique_ptr<Asset> Asset_importTexture(AssetMetadata &metadata, AssetHandle handle);

/**
 * @brief Builds a cubemap from the six external images its metadata names
 * @param metadata The asset's metadata, naming the face list to read
 * @param handle The handle the asset is registered under, set before any async load reports against it
 * @return The imported asset, or nullptr if the images could not be read
 */
std::unique_ptr<Asset> Asset_importCubemap(AssetMetadata &metadata, AssetHandle handle);

} // namespace Rapture

#endif // RAPTURE__ASSET_IMPORTER_H

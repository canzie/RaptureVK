#include "AssetManagerEditor.h"
#include "AssetImporter.h"
#include "Logging/Log.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Textures/Texture.h"
#include "Utils/UUID.h"
#include "WindowContext/Application.h"

#include <filesystem>

namespace Rapture {

AssetManagerEditor::AssetManagerEditor() : AssetManagerBase()
{
    // Initialize asset registry
}

AssetManagerEditor::~AssetManagerEditor()
{
    // Clean up resources
    // for (auto& [handle, asset] : m_loadedAssets) {

    // Release any resource handles
    //}
    m_loadedAssets.clear();
    m_assetRegistry.clear();
    m_defaultAssetHandles.clear();
}

bool AssetManagerEditor::isAssetHandleValid(AssetHandle handle) const
{
    return m_assetRegistry.find(handle) != m_assetRegistry.end();
}

bool AssetManagerEditor::isAssetLoaded(AssetHandle handle) const
{
    return m_loadedAssets.find(handle) != m_loadedAssets.end() && m_loadedAssets.at(handle)->isValid();
}

std::shared_ptr<Asset> AssetManagerEditor::getAsset(AssetHandle handle)
{

    if (!isAssetHandleValid(handle)) {
        RP_CORE_ERROR("Invalid asset handle");
        return nullptr;
    }

    // Check if asset is already loaded
    if (isAssetLoaded(handle)) {
        return m_loadedAssets.at(handle);
    } else {
        // Import asset
        const AssetMetadata &metadata = getAssetMetadata(handle);
        auto asset = AssetImporter::importAsset(handle, metadata);

        // Cache the loaded asset
        if (asset) {
            m_loadedAssets.insert_or_assign(handle, asset);
        } else {
            RP_CORE_ERROR("Failed to load asset: {}", metadata.m_filePath.string());
        }

        return asset;
    }
}

const AssetMetadata &AssetManagerEditor::getAssetMetadata(AssetHandle handle) const
{
    static AssetMetadata s_nullMetadata;

    auto it = m_assetRegistry.find(handle);
    if (it != m_assetRegistry.end()) {
        return it->second;
    }
    return s_nullMetadata;
}

std::pair<std::shared_ptr<Asset>, AssetHandle> AssetManagerEditor::importAsset(std::filesystem::path path,
                                                                               AssetImportConfigVariant importConfig)
{

    if (path.empty()) {
        RP_CORE_ERROR("Path is empty");
        return std::make_pair(nullptr, AssetHandle());
    }

    // 1. check if the asset is already in the registry
    for (const auto &[handle, metadata] : m_assetRegistry) {
        if (metadata.m_filePath == path && metadata.m_importConfig == importConfig) {
            return std::make_pair(getAsset(handle), handle);
        }
    }

    AssetMetadata metadata;
    metadata.m_storageType = AssetStorageType::Disk;
    metadata.m_filePath = path;

    metadata.assetType = determineAssetType(path.string());

    metadata.m_indices = {};
    metadata.m_importConfig = importConfig;

    if (metadata.assetType == AssetType::None) {
        RP_CORE_ERROR("Unknown asset type for extension: {}", path.extension().string());
        return std::make_pair(nullptr, AssetHandle());
    }

    // generate a handle for the asset
    AssetHandle handle = UUIDGenerator::Generate();

    auto asset = AssetImporter::importAsset(handle, metadata);
    if (asset && asset->isValid()) {
        m_assetRegistry.insert_or_assign(handle, metadata);
        m_loadedAssets.insert_or_assign(handle, asset);

        return std::make_pair(asset, handle);
    }

    RP_CORE_ERROR("Failed to import asset: {}", path.string());
    return std::make_pair(nullptr, AssetHandle());
}

std::pair<std::shared_ptr<Asset>, AssetHandle> AssetManagerEditor::importDefaultAsset(AssetType assetType)
{
    // Check if we already have a default asset of this type
    auto it = m_defaultAssetHandles.find(assetType);
    if (it != m_defaultAssetHandles.end()) {
        AssetHandle existingHandle = it->second;
        // Verify the asset is still loaded and valid
        if (isAssetLoaded(existingHandle)) {
            return std::make_pair(getAsset(existingHandle), existingHandle);
        } else {
            // Asset was unloaded somehow, remove the handle and recreate
            RP_CORE_WARN("Default {} asset was unloaded, recreating", AssetTypeToString(assetType));
            m_defaultAssetHandles.erase(it);
        }
    }

    switch (assetType) {
    case AssetType::Texture: {
        // Create default white texture
        auto defaultTexture = Texture::createDefaultWhiteTexture();
        if (!defaultTexture) {
            RP_CORE_ERROR("Failed to create default white texture");
            return std::make_pair(nullptr, AssetHandle());
        }
        defaultTexture->setReadyForSampling(true);

        // Generate a handle for the default texture
        AssetHandle handle = UUIDGenerator::Generate();

        // Create metadata for the default texture
        AssetMetadata metadata;
        metadata.assetType = AssetType::Texture;
        metadata.m_storageType = AssetStorageType::Disk; // Default assets are treated as disk assets
        metadata.m_filePath = "<default_white_texture>"; // Special path to indicate default asset
        metadata.m_indices = {0};
        metadata.m_importConfig = std::monostate(); // Initialize import config

        // Wrap the texture in an Asset object
        auto asset = std::make_shared<Asset>(AssetVariant{defaultTexture});
        asset->m_handle = handle;
        asset->m_status = AssetStatus::LOADED;

        // Register the asset
        m_assetRegistry.insert_or_assign(handle, metadata);
        m_loadedAssets.insert_or_assign(handle, asset);

        // Track this as a default asset
        m_defaultAssetHandles[assetType] = handle;

        RP_CORE_INFO("Created default white texture with handle");
        return std::make_pair(asset, handle);
    }
    case AssetType::Material: {
        // Create default material
        auto baseMaterial = MaterialManager::getMaterial("PBR");
        if (!baseMaterial) {
            RP_CORE_ERROR("Failed to get default material");
            return std::make_pair(nullptr, AssetHandle());
        }

        auto defaultMaterial = std::make_shared<MaterialInstance>(baseMaterial, "Default");
        if (!defaultMaterial) {
            RP_CORE_ERROR("Failed to create default material");
            return std::make_pair(nullptr, AssetHandle());
        }

        // Generate a handle for the default material
        AssetHandle handle = UUIDGenerator::Generate();

        // Create metadata for the default material
        AssetMetadata metadata;
        metadata.assetType = AssetType::Material;
        metadata.m_storageType = AssetStorageType::Disk;
        metadata.m_filePath = "<default_material>";
        metadata.m_indices = {};
        metadata.m_importConfig = std::monostate();

        auto asset = std::make_shared<Asset>(AssetVariant{defaultMaterial});
        asset->m_handle = handle;
        asset->m_status = AssetStatus::LOADED;

        // Register the asset
        m_assetRegistry.insert_or_assign(handle, metadata);
        m_loadedAssets.insert_or_assign(handle, asset);

        // Track this as a default asset
        m_defaultAssetHandles[assetType] = handle;

        return std::make_pair(asset, handle);
    }
    default:
        RP_CORE_WARN("Default asset type {} not implemented", AssetTypeToString(assetType));
        return std::make_pair(nullptr, AssetHandle());
    }
}

AssetType AssetManagerEditor::determineAssetType(const std::string &path)
{
    std::filesystem::path filePath(path);
    std::string extension = filePath.extension().string();

    // Convert to lowercase for case-insensitive comparison
    for (char &c : extension) {
        c = std::tolower(c);
    }

    // Determine asset type based on file extension
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp" ||
        extension == ".hdr") {
        return AssetType::Texture;
    } else if (extension == ".cubemap") {
        return AssetType::Cubemap;
    } else if (extension == ".gltf") {
        return AssetType::None;
    } else if (extension == ".rmat") {
        return AssetType::Material;
    } else if (extension == ".spv" || extension == ".glsl") {
        return AssetType::Shader;
    }
    // Add more asset types as needed

    RP_CORE_WARN("Unknown asset type for extension: {}", extension);
    return AssetType::None;
}

AssetHandle AssetManagerEditor::registerVirtualAsset(AssetVariant asset, const std::string &virtualName, AssetType assetType)
{
    if (std::holds_alternative<std::monostate>(asset)) {
        RP_CORE_ERROR("Asset variant is empty");
        return AssetHandle{};
    }

    if (virtualName.empty()) {
        RP_CORE_ERROR("Virtual name cannot be empty");
        return AssetHandle{};
    }

    // Check if virtual asset with this name already exists
    for (const auto &[handle, metadata] : m_assetRegistry) {
        if (metadata.isVirtualAsset() && metadata.m_virtualName == virtualName && metadata.assetType != assetType) {
            RP_CORE_WARN("Virtual asset with name '{}' already exists, returning existing handle", virtualName);
            return handle;
        }
    }

    // Generate new handle
    AssetHandle handle = UUIDGenerator::Generate();

    // Create Asset wrapper
    auto assetWrapper = std::make_shared<Asset>(std::move(asset));
    assetWrapper->m_handle = handle;
    assetWrapper->m_status = AssetStatus::LOADED; // Virtual assets are immediately loaded

    // Create metadata
    AssetMetadata metadata;
    metadata.assetType = assetType;
    metadata.m_storageType = AssetStorageType::Virtual;
    metadata.m_virtualName = virtualName;

    // Store in maps
    m_loadedAssets[handle] = assetWrapper;
    m_assetRegistry[handle] = metadata;

    RP_CORE_INFO("Registered virtual {} asset: '{}'", AssetTypeToString(assetType), virtualName);
    return handle;
}

bool AssetManagerEditor::unregisterVirtualAsset(AssetHandle handle)
{
    auto registryIt = m_assetRegistry.find(handle);
    if (registryIt == m_assetRegistry.end()) {
        RP_CORE_WARN("Asset handle not found in registry");
        return false;
    }

    const AssetMetadata &metadata = registryIt->second;
    if (!metadata.isVirtualAsset()) {
        RP_CORE_ERROR("Cannot unregister non-virtual asset: {}", metadata.m_filePath.string());
        return false;
    }

    // Remove from both maps
    m_loadedAssets.erase(handle);
    m_assetRegistry.erase(handle);

    RP_CORE_INFO("Unregistered virtual asset: '{}'", metadata.m_virtualName);
    return true;
}

AssetHandle AssetManagerEditor::getVirtualAssetByName(const std::string &virtualName) const
{
    for (const auto &[handle, metadata] : m_assetRegistry) {
        if (metadata.isVirtualAsset() && metadata.m_virtualName == virtualName) {
            return handle;
        }
    }
    return AssetHandle{}; // Invalid handle if not found
}

std::vector<AssetHandle> AssetManagerEditor::getVirtualAssetsByType(AssetType type) const
{
    std::vector<AssetHandle> result;
    for (const auto &[handle, metadata] : m_assetRegistry) {
        if (metadata.isVirtualAsset() && metadata.assetType == type) {
            result.push_back(handle);
        }
    }
    return result;
}

} // namespace Rapture

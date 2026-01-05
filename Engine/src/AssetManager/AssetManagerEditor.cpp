#include "AssetManagerEditor.h"
#include "AssetImporter.h"
#include "AssetManager/Asset.h"
#include "Logging/Log.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Textures/Texture.h"
#include "Utils/UUID.h"
#include "WindowContext/Application.h"

#include <filesystem>
#include <memory>

namespace Rapture {

AssetManagerEditor::AssetManagerEditor() : AssetManagerBase() {}

AssetManagerEditor::~AssetManagerEditor()
{
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

Asset &AssetManagerEditor::getAsset(AssetHandle handle)
{

    if (!isAssetHandleValid(handle)) {
        RP_CORE_ERROR("Invalid asset handle");
        return Asset::null;
    }

    if (isAssetLoaded(handle)) {
        return *m_loadedAssets.at(handle);
    } else {
        const AssetMetadata &metadata = getAssetMetadata(handle);
        if (!metadata) {
            RP_CORE_ERROR("Invalid asset metadata, import asset first");
            return Asset::null;
        }
        auto asset = std::make_unique<Asset>(handle);
        bool success = AssetImporter::importAsset(*asset, metadata);

        if (success) {
            auto [it, inserted] = m_loadedAssets.insert_or_assign(handle, std::move(asset));
            return *it->second;
        } else {
            RP_CORE_ERROR("Failed to load asset: {}", metadata.filePath.string());
            return Asset::null;
        }
    }
}

AssetMetadata &AssetManagerEditor::getAssetMetadata(AssetHandle handle)
{
    static AssetMetadata s_nullMetadata;

    auto it = m_assetRegistry.find(handle);
    if (it != m_assetRegistry.end()) {
        return it->second;
    }
    return s_nullMetadata;
}

Asset &AssetManagerEditor::importAsset(std::filesystem::path path, AssetImportConfigVariant importConfig)
{

    if (path.empty()) {
        RP_CORE_ERROR("Path is empty");
        return Asset::null;
    }

    for (const auto &[handle, metadata] : m_assetRegistry) {
        if (metadata.filePath == path && metadata.importConfig == importConfig) {
            return getAsset(handle);
        }
    }

    AssetMetadata metadata;
    metadata.storageType = AssetStorageType::DISK;
    metadata.filePath = path;

    metadata.assetType = determineAssetType(path.string());

    metadata.indices = {};
    metadata.importConfig = importConfig;

    if (!metadata) {
        RP_CORE_ERROR("Unknown asset type for extension: {}", path.extension().string());
        return Asset::null;
    }

    AssetHandle handle = UUIDGenerator::Generate();
    auto asset = std::make_unique<Asset>(handle);
    bool success = AssetImporter::importAsset(*asset, metadata);
    if (success) {
        m_assetRegistry.insert_or_assign(handle, metadata);
        auto [it, _] = m_loadedAssets.insert_or_assign(handle, std::move(asset));

        return *it->second;
    }

    RP_CORE_ERROR("Failed to import asset: {}", path.string());
    return Asset::null;
}

Asset &AssetManagerEditor::importDefaultAsset(AssetType assetType)
{
    auto it = m_defaultAssetHandles.find(assetType);
    if (it != m_defaultAssetHandles.end()) {
        AssetHandle existingHandle = it->second;
        // Verify the asset is still loaded and valid
        if (isAssetLoaded(existingHandle)) {
            return getAsset(existingHandle);
        } else {
            // Asset was unloaded somehow, remove the handle and recreate
            RP_CORE_WARN("Default {} asset was unloaded, recreating", AssetTypeToString(assetType));
            m_defaultAssetHandles.erase(it);
        }
    }

    switch (assetType) {
    case AssetType::TEXTURE: {
        auto defaultTexture = Texture::createDefaultWhiteTexture();
        if (!defaultTexture) {
            RP_CORE_ERROR("Failed to create default white texture");
            return Asset::null;
        }

        AssetHandle handle = UUIDGenerator::Generate();

        AssetMetadata metadata;
        metadata.assetType = AssetType::TEXTURE;
        metadata.storageType = AssetStorageType::DISK; // Default assets are treated as disk assets
        metadata.filePath = "<default_white_texture>"; // Special path to indicate default asset
        metadata.indices = {0};
        metadata.importConfig = std::monostate();

        m_assetRegistry.insert_or_assign(handle, metadata);
        auto [it, _] = m_loadedAssets.insert_or_assign(handle, std::make_unique<Asset>(std::move(defaultTexture), handle));
        it->second->status = AssetStatus::LOADED;
        m_defaultAssetHandles[assetType] = handle;

        RP_CORE_INFO("Created default white texture with handle");
        return *it->second;
    }
    case AssetType::MATERIAL: {
        auto baseMaterial = MaterialManager::getMaterial("PBR");
        if (!baseMaterial) {
            RP_CORE_ERROR("Failed to get default material");
            return Asset::null;
        }

        auto defaultMaterial = std::make_unique<MaterialInstance>(baseMaterial, "Default");
        if (!defaultMaterial) {
            RP_CORE_ERROR("Failed to create default material");
            return Asset::null;
        }

        AssetHandle handle = UUIDGenerator::Generate();

        AssetMetadata metadata;
        metadata.assetType = AssetType::MATERIAL;
        metadata.storageType = AssetStorageType::VIRTUAL;
        metadata.virtualName = "<default_material>";
        metadata.indices = {};
        metadata.importConfig = std::monostate();

        // Register the asset
        m_assetRegistry.insert_or_assign(handle, metadata);
        auto [it, _] = m_loadedAssets.insert_or_assign(handle, std::make_unique<Asset>(std::move(defaultMaterial), handle));
        it->second->status = AssetStatus::LOADED;
        // Track this as a default asset
        m_defaultAssetHandles[assetType] = handle;

        return *it->second;
    }
    default:
        RP_CORE_WARN("Default asset type {} not implemented", AssetTypeToString(assetType));
        return Asset::null;
    }
}

AssetType AssetManagerEditor::determineAssetType(const std::string &path)
{
    std::filesystem::path filePath(path);
    std::string extension = filePath.extension().string();

    for (char &c : extension) {
        c = std::tolower(c);
    }

    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp" ||
        extension == ".hdr") {
        return AssetType::TEXTURE;
    } else if (extension == ".cubemap") {
        return AssetType::CUBEMAP;
    } else if (extension == ".gltf" || extension == ".glb" || extension == ".fbx") {
        return AssetType::SCENE;
    } else if (extension == ".rmat") {
        return AssetType::MATERIAL;
    } else if (extension == ".spv" || extension == ".glsl") {
        return AssetType::SHADER;
    }

    RP_CORE_WARN("Unknown asset type for extension: {}", extension);
    return AssetType::NONE;
}

Asset &AssetManagerEditor::registerVirtualAsset(AssetVariant assetValue, const std::string &virtualName, AssetType assetType)
{
    if (std::holds_alternative<std::monostate>(assetValue)) {
        RP_CORE_ERROR("Asset variant is empty");
        return Asset::null;
    }

    if (virtualName.empty()) {
        RP_CORE_ERROR("Virtual name cannot be empty");
        return Asset::null;
    }

    for (const auto &[handle, metadata] : m_assetRegistry) {
        if (metadata.isVirtualAsset() && metadata.virtualName == virtualName && metadata.assetType != assetType) {
            RP_CORE_WARN("Virtual asset with name '{}' already exists", virtualName);
            return Asset::null;
        }
    }

    AssetHandle handle = UUIDGenerator::Generate();
    auto asset = std::make_unique<Asset>(std::move(assetValue), handle);
    asset->status = AssetStatus::LOADED; // Virtual assets are immediately loaded

    AssetMetadata metadata;
    metadata.assetType = assetType;
    metadata.storageType = AssetStorageType::VIRTUAL;
    metadata.virtualName = virtualName;

    auto [it, _] = m_loadedAssets.insert_or_assign(handle, std::move(asset));
    m_assetRegistry.insert_or_assign(handle, metadata);

    RP_CORE_INFO("Registered virtual {} asset: '{}'", AssetTypeToString(assetType), virtualName);
    return *it->second;
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
        RP_CORE_ERROR("Cannot unregister non-virtual asset: {}", metadata.filePath.string());
        return false;
    }

    m_loadedAssets.erase(handle);
    m_assetRegistry.erase(handle);

    RP_CORE_INFO("Unregistered virtual asset: '{}'", metadata.virtualName);
    return true;
}

Asset &AssetManagerEditor::getVirtualAssetByName(const std::string &virtualName)
{
    for (const auto &[handle, metadata] : m_assetRegistry) {
        if (metadata.isVirtualAsset() && metadata.virtualName == virtualName) {
            return getAsset(handle);
        }
    }
    return Asset::null;
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

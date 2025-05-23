#include "AssetManagerEditor.h"
#include "AssetImporter.h"
#include "Utils/UUID.h"
#include "Logging/Log.h"
#include "Materials/Material.h"

#include "WindowContext/Application.h"

#include <filesystem>

namespace Rapture {


    AssetManagerEditor::AssetManagerEditor()
    : AssetManagerBase()
    {
        // Initialize asset registry
    }

    AssetManagerEditor::~AssetManagerEditor() {
        // Clean up resources
        for (auto& [handle, asset] : m_loadedAssets) {
            // Release any resource handles
        }
        m_loadedAssets.clear();
        m_assetRegistry.clear();
    }

    bool AssetManagerEditor::isAssetHandleValid(AssetHandle handle) const {
        return m_assetRegistry.find(handle) != m_assetRegistry.end();
    }

    bool AssetManagerEditor::isAssetLoaded(AssetHandle handle) const {
        return m_loadedAssets.find(handle) != m_loadedAssets.end() && m_loadedAssets.at(handle)->isValid();
    }

    std::shared_ptr<Asset> AssetManagerEditor::getAsset(AssetHandle handle) {
        
        if (!isAssetHandleValid(handle)) {
            RP_CORE_ERROR("AssetManagerEditor::getAsset - Invalid asset handle");
            return nullptr;
        }

        // Check if asset is already loaded
        if (isAssetLoaded(handle)) {
            return m_loadedAssets.at(handle);
        } else {
            // Import asset
            const AssetMetadata& metadata = getAssetMetadata(handle);
            auto asset = AssetImporter::importAsset(handle, metadata);
            
            // Cache the loaded asset
            if (asset) {
                m_loadedAssets.insert_or_assign(handle, asset);
            } else {
                RP_CORE_ERROR("AssetManagerEditor::getAsset - Failed to load asset: {}", metadata.m_filePath.string());
            }
            
            return asset;
        }
    }

    const AssetMetadata& AssetManagerEditor::getAssetMetadata(AssetHandle handle) const {
        static AssetMetadata s_nullMetadata;

        auto it = m_assetRegistry.find(handle);
        if (it != m_assetRegistry.end()) {
            return it->second;
        }
        return s_nullMetadata;
    }

    std::pair<std::shared_ptr<Asset>, AssetHandle> AssetManagerEditor::importAsset(std::filesystem::path path, std::vector<uint32_t> indices, AssetType assetType)
    {



        if (path.empty()) {
            RP_CORE_ERROR("AssetManagerEditor::importAsset - Path is empty");
            return std::make_pair(nullptr, AssetHandle());
        }

        // 1. check if the asset is already in the registry
        for (const auto& [handle, metadata] : m_assetRegistry) {
            if (metadata.m_filePath == path && metadata.m_indices == indices) {
                return std::make_pair(getAsset(handle), handle);
            }
        }


        AssetMetadata metadata;
        metadata.m_filePath = path;

        if (assetType == AssetType::None) {
            metadata.m_assetType = determineAssetType(path.string());
        } else {
            metadata.m_assetType = assetType;
        }

        metadata.m_indices = indices;

        if (metadata.m_assetType == AssetType::None) {
            RP_CORE_ERROR("AssetManagerEditor::importAsset - Unknown asset type for extension: {}", path.extension().string());
            return std::make_pair(nullptr, AssetHandle());
        }

        // generate a handle for the asset
        AssetHandle handle = UUIDGenerator::Generate();


        auto asset = AssetImporter::importAsset(handle, metadata);
        if (asset) {
            m_assetRegistry.insert_or_assign(handle, metadata);
            m_loadedAssets.insert_or_assign(handle, asset);

            return std::make_pair(asset, handle);

        } 

        RP_CORE_ERROR("AssetManagerEditor::importAsset - Failed to import asset: {}", path.string());
        return std::make_pair(nullptr, AssetHandle());


    }


    AssetType AssetManagerEditor::determineAssetType(const std::string& path) {
        std::filesystem::path filePath(path);
        std::string extension = filePath.extension().string();
        
        // Convert to lowercase for case-insensitive comparison
        for (char& c : extension) {
            c = std::tolower(c);
        }
        
        // Determine asset type based on file extension
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || 
            extension == ".tga" || extension == ".bmp" || extension == ".hdr") {
            return AssetType::None;
        }
        else if (extension == ".gltf") {
            return AssetType::None;
        }
        else if (extension == ".rmat") {
            return AssetType::Material;
        } else if (extension == ".spv") {
            return AssetType::Shader;
        }
        // Add more asset types as needed
        
        RP_CORE_WARN("AssetManagerEditor::determineAssetType - Unknown asset type for extension: {}", extension);
        return AssetType::None;
    }


}

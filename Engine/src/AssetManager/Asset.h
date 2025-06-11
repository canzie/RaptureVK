#pragma once


#include "Utils/UUID.h"

#include <filesystem>
#include <cstdint>
#include <variant>
#include <string>

namespace Rapture {

    // Forward declarations to break circular dependency
    class Material;
    class Shader; // Forward declare Shader
    class Texture;

    using AssetHandle = UUID;
    // NOTE: i dont like this but dont know variants well enough and dont want to change the entire codebase
    using AssetVariant = std::variant<std::monostate, std::shared_ptr<Material>, std::shared_ptr<Shader>, std::shared_ptr<Texture>>;


    enum class AssetType {
        None = 0,
        Material,
        Shader,
        Texture
    };

    enum class AssetStorageType {
        Disk,
        Virtual
    };

    enum class AssetStatus {
        REQUESTED,
        LOADING,
        LOADED,
        FAILED
    };

    inline std::string AssetTypeToString(AssetType type) {
        switch (type) {
            case AssetType::None: return "None";
            case AssetType::Material: return "Material";
            case AssetType::Shader: return "Shader";
            case AssetType::Texture: return "Texture";
            default: return "Unknown";
        }
    }

    struct AssetMetadata {

        AssetType m_assetType = AssetType::None;
        AssetStorageType m_storageType = AssetStorageType::Disk;
        
        // Disk-specific data (only used when m_storageType == Disk)
        std::filesystem::path m_filePath;
        // some assets might be in the same file, the indices should point to them
        // indices will be mostly 1 element, but in case of loading multiple primitives in 1 static mesh
        // the indices will indicate which ones
        std::vector<uint32_t> m_indices;
        
        // Virtual-specific data (only used when m_storageType == Virtual)
        std::string m_virtualName;


        operator bool() const {
            return m_assetType != AssetType::None;
        }
        
        bool isDiskAsset() const { return m_storageType == AssetStorageType::Disk; }
        bool isVirtualAsset() const { return m_storageType == AssetStorageType::Virtual; }

    };


    class Asset {
    public:
        Asset(std::shared_ptr<AssetVariant> asset) : m_asset(asset) { }

        template<typename T>
        std::shared_ptr<T> getUnderlyingAsset() const {
            if (std::holds_alternative<std::shared_ptr<T>>(*m_asset)) {
                return std::get<std::shared_ptr<T>>(*m_asset);
            }
            return nullptr;
        }

        bool isValid() const {
            return !std::holds_alternative<std::monostate>(*m_asset);
        }


    public:
        AssetHandle m_handle;
        AssetStatus m_status = AssetStatus::REQUESTED;
        //AssetType getAssetType() const;

    private:
        std::shared_ptr<AssetVariant> m_asset;
    
    
    };
}

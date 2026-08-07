#ifndef RAPTURE__ASSETCOMMON_H
#define RAPTURE__ASSETCOMMON_H

#include "utils/UUID.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Rapture {

using AssetHandle = UUID;

static constexpr AssetHandle INVALID_ASSET_HANDLE = 0;

/**
 * @brief Where an asset was originally imported from, kept for reimport not for loading
 */
struct AssetProvenance {
    std::filesystem::path sourcePath;                      // original file, empty if generated
    std::optional<uint32_t> sourceSubIndex = std::nullopt; // sub-asset index within a container source, e.g. a glTF mesh
};

enum class AssetType {
    NONE,
    TEXTURE,
    CUBEMAP,
    SHADER,
    MATERIAL,
    MATERIAL_INSTANCE,
    MESH,
    PREFAB,
    ANIMATION,
    AUDIO,
    VIDEO,
    SCENE, // glTF, fbx, etc
    MODULE
};

enum class AssetStorageType {
    DISK,
    VIRTUAL
};

enum class AssetStatus {
    REQUESTED,
    LOADING,
    LOADED,
    FAILED,
    FILE_NOT_FOUND
};

enum class AssetEvictionPolicy {
    EVICT_IMMEDIATE, // evict as soon as useCount hits 0
    EVICT_HINT_LAZY, // useCount 0 -> cold LRU list, freed under memory-budget pressure
    EVICT_HINT_LAST  // "keep loaded" hint, evicted last under pressure, never a guarantee
};

inline std::string AssetTypeToString(AssetType type)
{
    switch (type) {
    case AssetType::TEXTURE:
        return "Texture";
    case AssetType::CUBEMAP:
        return "Cubemap";
    case AssetType::SHADER:
        return "Shader";
    case AssetType::MATERIAL:
        return "Material";
    case AssetType::MATERIAL_INSTANCE:
        return "Material Instance";
    case AssetType::MESH:
        return "Mesh";
    case AssetType::PREFAB:
        return "Prefab";
    case AssetType::MODULE:
        return "Module";
    case AssetType::ANIMATION:
        return "Animation";
    case AssetType::AUDIO:
        return "Audio";
    case AssetType::VIDEO:
        return "Video";
    case AssetType::SCENE:
        return "Scene";
    default:
        return "Unknown";
    }
}

} // namespace Rapture
#endif // RAPTURE__ASSETCOMMON_H

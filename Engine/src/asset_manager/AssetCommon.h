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

enum AssetType {
    ASSET_NONE,
    ASSET_TEXTURE,
    ASSET_CUBEMAP,
    ASSET_SHADER,
    ASSET_MATERIAL,
    ASSET_MATERIAL_INSTANCE,
    ASSET_MESH,
    ASSET_SCENE_OBJECT,
    ASSET_ANIMATION,
    ASSET_AUDIO,
    ASSET_VIDEO,
    ASSET_MODULE,
    ASSET_WORLD,
    ASSET_TYPE_COUNT
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
    case ASSET_TEXTURE:
        return "Texture";
    case ASSET_CUBEMAP:
        return "Cubemap";
    case ASSET_SHADER:
        return "Shader";
    case ASSET_MATERIAL:
        return "Material";
    case ASSET_MATERIAL_INSTANCE:
        return "Material Instance";
    case ASSET_MESH:
        return "Mesh";
    case ASSET_SCENE_OBJECT:
        return "Scene Object";
    case ASSET_MODULE:
        return "Module";
    case ASSET_ANIMATION:
        return "Animation";
    case ASSET_AUDIO:
        return "Audio";
    case ASSET_VIDEO:
        return "Video";
    case ASSET_WORLD:
        return "World";
    default:
        return "Unknown";
    }
}

} // namespace Rapture
#endif // RAPTURE__ASSETCOMMON_H

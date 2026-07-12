#ifndef RAPTURE__ASSETCOMMON_H
#define RAPTURE__ASSETCOMMON_H

#include "utils/UUID.h"

#include <string>

namespace Rapture {

using AssetHandle = UUID;

enum class AssetType {
    NONE,
    TEXTURE,
    CUBEMAP,
    SHADER,
    MATERIAL,
    MESH,
    MODEL,
    ANIMATION,
    AUDIO,
    VIDEO,
    SCENE // glTF, fbx, etc
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
    case AssetType::MESH:
        return "Mesh";
    case AssetType::MODEL:
        return "Model";
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

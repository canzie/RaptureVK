#ifndef RAPTURE__ASSETCOMMON_H
#define RAPTURE__ASSETCOMMON_H

#include "Utils/UUID.h"

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

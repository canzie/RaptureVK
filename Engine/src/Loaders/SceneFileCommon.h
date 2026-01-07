#ifndef RAPTURE__SCENE_FILE_COMMON_H
#define RAPTURE__SCENE_FILE_COMMON_H

#include "AssetManager/AssetCommon.h"
#include <filesystem>

namespace Rapture {

struct SceneFileMetadata {
    std::filesystem::path sourcePath;

    size_t meshCount = 0;
    size_t materialCount = 0;
    size_t animationCount = 0;
    size_t nodeCount = 0;
    size_t textureCount = 0;

    bool hasSkeletons = false;

    std::string generator;
    std::string version;
};

struct SceneFileData {
    SceneFileMetadata metadata;

    std::vector<AssetHandle> meshes;
    std::vector<AssetHandle> materials;
    std::vector<AssetHandle> animations;
};

} // namespace Rapture

#endif // RAPTURE__SCENE_FILE_COMMON_H

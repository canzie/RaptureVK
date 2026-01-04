#ifndef RAPTURE__GLTF_COMMON_H
#define RAPTURE__GLTF_COMMON_H

#include "AssetManager/AssetCommon.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

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

#endif // RAPTURE__GLTF_COMMON_H

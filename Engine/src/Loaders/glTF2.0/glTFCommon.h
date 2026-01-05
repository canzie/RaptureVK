#ifndef RAPTURE__GLTF_COMMON_H
#define RAPTURE__GLTF_COMMON_H

#include "AssetManager/Asset.h"
#include "Loaders/SceneFileCommon.h"

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Rapture {

/**
 * @brief Type of a glTF node for processing purposes
 */
enum class glTF_NodeType {
    EMPTY,      ///< Node with no mesh (transform node or group)
    PRIMITIVE,  ///< Node containing a single primitive (renderable)
    SKELETON,   ///< Node containing skeleton data
    BONE,       ///< Bone node within a skeleton
};

/**
 * @brief Represents a single node in the parsed glTF scene graph
 *
 * Built during async parsing and later converted to ECS entities
 */
struct glTF_SceneNode {
    std::string name;
    glTF_NodeType type = glTF_NodeType::EMPTY;

    glm::mat4 worldTransform = glm::mat4(1.0f);

    AssetRef meshRef;           ///< Mesh asset reference (registered with AssetManager)
    int32_t materialIndex = -1; ///< glTF file material index (-1 if no material)

    glm::vec3 boundingBoxMin = glm::vec3(0.0f);
    glm::vec3 boundingBoxMax = glm::vec3(0.0f);
    bool hasBoundingBox = false;

    std::vector<std::unique_ptr<glTF_SceneNode>> children;
    glTF_SceneNode *parent = nullptr;
};

/**
 * @brief Contains all data loaded from a glTF file
 *
 * Built during async loading, then finalized to ECS on main thread
 */
struct glTF_LoadedSceneData {
    std::unordered_map<size_t, AssetRef> materials;  ///< Keyed by glTF material index

    std::vector<std::unique_ptr<glTF_SceneNode>> rootNodes;

    SceneFileMetadata metadata;
};

} // namespace Rapture

#endif // RAPTURE__GLTF_COMMON_H

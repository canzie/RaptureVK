#ifndef RAPTURE__PREFAB_H
#define RAPTURE__PREFAB_H

#include "asset_manager/AssetCommon.h"
#include "asset_manager/AssetHandle.h"
#include "events/EventSignal.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Rapture {

class Scene;
class Entity;

/**
 * @brief A blueprint tree of entities referencing meshes and materials by handle
 *
 * The tree is a flat array whose nodes link to their parent by index. A prefab owns no GPU data
 * and pins nothing. Instantiating it spawns one grabbable root entity with the tree expanded
 * beneath it.
 */
class Prefab {
  public:
    struct Node {
        std::string name;
        int32_t parent = -1; ///< index into the node array, -1 for a root
        glm::mat4 localTransform = glm::mat4(1.0f);

        AssetHandle mesh = INVALID_ASSET_HANDLE;     ///< no mesh = group/transform node
        AssetHandle material = INVALID_ASSET_HANDLE; ///< default material for this node's mesh

        glm::vec3 boundingBoxMin = glm::vec3(0.0f);
        glm::vec3 boundingBoxMax = glm::vec3(0.0f);

        bool hasMesh() const { return mesh != INVALID_ASSET_HANDLE; }
        bool hasBoundingBox() const { return glm::any(glm::notEqual(boundingBoxMin, boundingBoxMax)); }
    };

    /**
     * @brief Instantiates a prefab into a scene under a single root entity
     * @param prefab An asset ref to the prefab, held by the instance root's PrefabComponent
     * @param scene The scene to spawn entities into
     * @param rootTransform World transform applied to the instance root
     * @return The root entity of the spawned instance, or a null entity on failure
     */
    static Entity instantiate(AssetRef prefab, Scene *scene, const glm::mat4 &rootTransform = glm::mat4(1.0f));

    /**
     * @brief Serializes this prefab tree into a self-contained blob
     * @return The serialized bytes
     */
    std::vector<uint8_t> serialize() const;

    /**
     * @brief Rebuilds a prefab from a blob produced by serialize
     * @param blob The serialized bytes
     * @return The prefab, or nullptr if the blob is invalid
     */
    static std::unique_ptr<Prefab> deserialize(std::span<const uint8_t> blob);

    std::string_view getName() const { return m_name; }
    const std::vector<Node> &getNodes() const { return m_nodes; }

    void setName(std::string name) { m_name = std::move(name); }

    /**
     * @brief Replaces the whole node array, signalling a structure change
     * @note Nodes must be pre-order, so each node's parent index is less than its own
     * @param nodes The new node array
     */
    void setNodes(std::vector<Node> nodes)
    {
        m_nodes = std::move(nodes);
        m_onStructureChanged.fire();
    }

    EventSignal<void()> &onStructureChanged() { return m_onStructureChanged; }

  private:
    std::string m_name;
    std::vector<Node> m_nodes;
    EventSignal<void()> m_onStructureChanged;
};

} // namespace Rapture

#endif // RAPTURE__PREFAB_H

#ifndef RAPTURE__MATERIAL_GRAPH_H
#define RAPTURE__MATERIAL_GRAPH_H

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "GraphDomain.h"
#include "MaterialGraphTypes.h"
#include "assets/asset_manager/Asset.h"
#include "assets/textures/ATexture.h"

namespace Rapture {

class Texture;

/**
 * @brief One placed node instance in a material graph
 */
struct GraphNode {
    uint32_t id = 0;
    GraphNodeType type = GraphNodeType::NONE;
    std::vector<std::optional<PinValue>> inputValues = {}; // authored numeric value per input pin, nullopt when unset
    std::vector<Ref<ATexture>> inputTextures = {};         // authored texture per texture input pin, null when unset
};

/**
 * @brief A directed link from a source node output pin to a destination node input pin
 */
struct GraphConnection {
    uint32_t srcNode = 0;
    uint32_t srcPin = 0;
    uint32_t dstNode = 0;
    uint32_t dstPin = 0;
};

/**
 * @brief An authored surface graph: its domain, nodes, connections, and the output node
 */
struct MaterialGraph {
    std::string name;
    GraphDomainId domain = GD_SURFACE; // which inputs the nodes may read and which passes it compiles to
    std::vector<GraphNode> nodes;
    std::vector<GraphConnection> connections;
    uint32_t outputNodeId = 0;

    /**
     * @brief Find a node by id
     * @param id The node id to look up
     * @return Pointer to the node, or nullptr if not present
     */
    const GraphNode *findNode(uint32_t id) const;

    /**
     * @brief Serializes this graph into a self-contained blob, texture inputs stored by handle
     * @return The serialized bytes
     */
    std::vector<uint8_t> serialize() const;

    /**
     * @brief Rebuilds a graph from a blob, resolving texture handles back to asset pointers
     * @param blob The serialized bytes
     * @return The graph, or empty on failure
     */
    static std::optional<MaterialGraph> deserialize(std::span<const uint8_t> blob);
};

} // namespace Rapture

#endif // RAPTURE__MATERIAL_GRAPH_H

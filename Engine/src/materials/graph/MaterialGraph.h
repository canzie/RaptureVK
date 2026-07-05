#ifndef RAPTURE__MATERIAL_GRAPH_H
#define RAPTURE__MATERIAL_GRAPH_H

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "MaterialGraphTypes.h"
#include "asset_manager/AssetHandle.h"

namespace Rapture {

class Texture;

/**
 * @brief One placed node instance in a material graph
 */
struct GraphNode {
    uint32_t id = 0;
    GraphNodeType type = GraphNodeType::NONE;
    glm::vec4 constantValue{0.0f};  // CONSTANT nodes: the slot value
    AssetPtr<Texture> texture = {};  // TEXTURE nodes: bound texture, bindless index read at compile
    glm::vec2 editorPosition{0.0f}; // editor only, ignored by the compiler
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
 * @brief An authored surface graph: nodes, connections, and the output node
 */
struct MaterialGraph {
    std::string name;
    std::vector<GraphNode> nodes;
    std::vector<GraphConnection> connections;
    uint32_t outputNodeId = 0;

    /**
     * @brief Find a node by id
     * @param id The node id to look up
     * @return Pointer to the node, or nullptr if not present
     */
    const GraphNode *findNode(uint32_t id) const;
};

} // namespace Rapture

#endif // RAPTURE__MATERIAL_GRAPH_H

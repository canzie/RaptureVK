#ifndef RAPTURE__GRAPH_H
#define RAPTURE__GRAPH_H

#include "Utils/UUID.h"

#include <functional>
#include <glm/glm.hpp>
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Modules {

struct GraphNode;

// clang-format off
enum ParameterType {
    U32, U64, UVEC2, UVEC3, UVEC4,
    I32, I64, IVEC2, IVEC3, IVEC4,
    F32, F64, VEC2,  VEC3,  VEC4,
    STRING,
    TEXTURE_HANDLE // uint32_t, but cannot do any math operations, it can be passed as is only or be used for input nodes
};
// clang-format on

enum class NodeOpType {
    INPUT,  // does not perform an action, just returns the outputs (last node)
    OUTPUT, // does not perform an action, just returns the inputs (first node)
    ADD,
    SUBTRACT,
    MULTIPLY,
    DIVIDE,
    MIX,    // mix(a, b, alpha) alpha=1 => b, alpha=0 => a
    CLAMP,  // clamp(a, min, max)
    LENGTH, // Length of a vector
    UNIT,
    NORMALIZE,
    SPLIT, // splits a type like a vec4 into x, y, z, w
    GROUP  // opposite of SPLIT
};

using NodeValue = std::variant<std::monostate, uint32_t, uint64_t, int32_t, int64_t, float, double, glm::vec2, glm::vec3, glm::vec4,
                               glm::ivec2, glm::ivec3, glm::ivec4, glm::uvec2, glm::uvec3, glm::uvec4, std::string>;

struct NodeParameter {
    std::string name;
    ParameterType pType;
    NodeValue value;
    ImVec4 color; // Visual color for the parameter pin

    NodeParameter() : name(""), pType(ParameterType::F32), value(0.0f), color(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)) {}
};

struct NodeConnection {
    uint32_t fromNode;
    uint32_t toNode;
    uint32_t outputIndex; // index of fromNode's output params
    uint32_t inputIndex;  // index of toNode's input params

    bool operator==(const NodeConnection &other)
    {
        return fromNode == other.fromNode && toNode == other.toNode && outputIndex == other.outputIndex &&
               inputIndex == other.inputIndex;
    }
};

struct GraphNode {
    uint32_t id;
    std::string name;
    NodeOpType opType;
    ImVec2 windowSize;
    ImVec2 windowPosition;
    ImVec4 color; // Visual color for the node

    std::vector<NodeConnection> connections;

    // the actual labels inside of a node
    std::vector<NodeParameter> inputs;
    std::vector<NodeParameter> outputs;

    GraphNode()
        : id(0), name(""), opType(NodeOpType::INPUT), windowSize(ImVec2(200, 200)), windowPosition(ImVec2(0, 0)),
          color(ImVec4(0.2f, 0.2f, 0.2f, 1.0f))
    {
    }
};

class Graph {
  public:
    // these in and outputs cannot be removed from the graph
    Graph(std::vector<GraphNode> inputs, std::vector<GraphNode> outputs);
    ~Graph() = default;

    /*
     * @brief Evaluate the given node, the values will be available in the nodes output parameter if true is returned
     * */
    bool evaluate(uint32_t nodeIndex);
    /*
     * @brief evaluate all of the final nodes, this evaluates the entire DAG's connected nodes
     * */
    bool evaluate();

    // false if not successful
    // we always want to copy
    uint32_t addNode(const GraphNode &node);
    bool removeNode(uint32_t id);
    bool link(const NodeConnection &connection);
    bool unlink(const NodeConnection &connection);

    // Access to nodes for rendering/editing
    std::unordered_map<uint32_t, GraphNode> &getNodes() { return m_nodes; }
    const std::unordered_map<uint32_t, GraphNode> &getNodes() const { return m_nodes; }

  private:
    // For the cases like when in and out are the same node
    // going from in to in or out to out
    // wrong types
    bool canConnect(NodeConnection connection);

    // Internal recursive evaluation with cycle detection
    bool evaluateNode(uint32_t nodeId, std::unordered_map<uint32_t, bool> &evaluated,
                      std::unordered_map<uint32_t, bool> &inProgress);

  private:
    std::vector<uint32_t> m_inputIds;
    std::vector<uint32_t> m_outputIds;
    std::unordered_map<uint32_t, GraphNode> m_nodes;
    std::unordered_map<NodeOpType, std::function<bool(GraphNode &)>> m_evaluators;
    Rapture::UidGenerator gen;
};

} // namespace Modules

#endif // RAPTURE__GRAPH_H

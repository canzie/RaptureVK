#include "Graph.h"
#include "GraphNodeEvaluators.h"

namespace Modules {

Graph::Graph(std::vector<GraphNode> inputs, std::vector<GraphNode> outputs)
{
    // Initialize evaluators
    initializeEvaluators(m_evaluators);

    for (GraphNode &input : inputs) {
        uint32_t index = addNode(input);
        m_inputIds.push_back(index);
    }
    for (GraphNode &output : outputs) {
        uint32_t index = addNode(output);
        m_outputIds.push_back(index);
    }
}

uint32_t Graph::addNode(const GraphNode &node)
{
    uint32_t id = gen.next();
    GraphNode copy = node;
    copy.id = id;
    m_nodes.insert({id, copy});

    return id;
}

bool Graph::removeNode(uint32_t id)
{
    auto it = m_nodes.find(id);
    if (it == m_nodes.end()) {
        return true;
    }

    GraphNode &node = it->second;
    for (auto &connection : node.connections) {
        unlink(connection);
    }

    m_nodes.erase(it);

    return true;
}

void s_removeConnection(GraphNode &node, const NodeConnection &connection)
{
    auto &connections = node.connections;
    for (auto i = 0; i < connections.size(); ++i) {
        auto &con = connections[i];
        if (con == connection) {
            connections.erase(connections.begin() + i);
            break;
        }
    }
}

bool s_addConnection(GraphNode &node, const NodeConnection &connection)
{
    auto &connections = node.connections;
    for (auto i = 0; i < connections.size(); ++i) {
        auto &con = connections[i];
        if (con == connection) {
            return false;
        }
    }

    connections.push_back(connection);
    return true;
}

bool Graph::unlink(const NodeConnection &connection)
{
    auto itFrom = m_nodes.find(connection.fromNode);
    auto itTo = m_nodes.find(connection.toNode);

    if (itFrom != m_nodes.end() && connection.outputIndex < itFrom->second.outputs.size()) {
        s_removeConnection(itFrom->second, connection);
    }
    if (itTo != m_nodes.end() && connection.inputIndex < itTo->second.inputs.size()) {
        s_removeConnection(itTo->second, connection);
    }

    return true;
}

bool Graph::link(const NodeConnection &connection)
{
    // Prevent self-connections
    if (connection.fromNode == connection.toNode) {
        return false;
    }

    auto itFrom = m_nodes.find(connection.fromNode);
    auto itTo = m_nodes.find(connection.toNode);

    if (itFrom == m_nodes.end() || connection.outputIndex >= itFrom->second.outputs.size()) {
        return false;
    }

    if (itTo == m_nodes.end() || connection.inputIndex >= itTo->second.inputs.size()) {
        return false;
    }

    // Check if the input already has a connection (inputs can only have ONE connection)
    auto &toNode = itTo->second;
    for (auto &conn : toNode.connections) {
        if (conn.toNode == connection.toNode && conn.inputIndex == connection.inputIndex) {
            // Input already has a connection, remove it first
            unlink(conn);
            break;
        }
    }

    // Now add the new connection
    if (!s_addConnection(itFrom->second, connection)) {
        return false;
    }

    if (!s_addConnection(itTo->second, connection)) {
        s_removeConnection(itFrom->second, connection);
        return false;
    }

    return true;
}

bool Graph::evaluateNode(uint32_t nodeId, std::unordered_map<uint32_t, bool> &evaluated,
                         std::unordered_map<uint32_t, bool> &inProgress)
{
    // Check if already evaluated
    if (evaluated[nodeId]) {
        return true;
    }

    // Check for cycles
    if (inProgress[nodeId]) {
        return false; // Cycle detected
    }

    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end()) {
        return false;
    }

    GraphNode &node = it->second;
    inProgress[nodeId] = true;

    // For each input parameter, check if there's a connection
    for (size_t inputIdx = 0; inputIdx < node.inputs.size(); ++inputIdx) {
        // Find connection that feeds this input (connections are stored in both nodes)
        NodeConnection *feedingConnection = nullptr;

        for (auto &conn : node.connections) {
            if (conn.toNode == nodeId && conn.inputIndex == inputIdx) {
                feedingConnection = &conn;
                break;
            }
        }

        // If there's a connection, evaluate the source node first
        if (feedingConnection) {
            // Recursively evaluate the source node
            if (!evaluateNode(feedingConnection->fromNode, evaluated, inProgress)) {
                inProgress[nodeId] = false;
                return false;
            }

            // Copy the output value from source to this input
            auto sourceIt = m_nodes.find(feedingConnection->fromNode);
            if (sourceIt != m_nodes.end()) {
                GraphNode &sourceNode = sourceIt->second;
                if (feedingConnection->outputIndex < sourceNode.outputs.size()) {
                    node.inputs[inputIdx].value = sourceNode.outputs[feedingConnection->outputIndex].value;
                    node.inputs[inputIdx].pType = sourceNode.outputs[feedingConnection->outputIndex].pType;
                }
            }
        }
        // If no connection, the input value should already be set manually
    }

    // Now all inputs are ready, evaluate this node
    auto evalIt = m_evaluators.find(node.opType);
    if (evalIt == m_evaluators.end()) {
        inProgress[nodeId] = false;
        return false;
    }

    bool success = evalIt->second(node);
    if (!success) {
        inProgress[nodeId] = false;
        return false;
    }

    // Mark as evaluated
    evaluated[nodeId] = true;
    inProgress[nodeId] = false;
    return true;
}

bool Graph::evaluate(uint32_t nodeId)
{
    std::unordered_map<uint32_t, bool> evaluated;
    std::unordered_map<uint32_t, bool> inProgress;

    return evaluateNode(nodeId, evaluated, inProgress);
}

bool Graph::evaluate()
{
    std::unordered_map<uint32_t, bool> evaluated;
    std::unordered_map<uint32_t, bool> inProgress;

    // Evaluate all output nodes
    for (uint32_t outputId : m_outputIds) {
        if (!evaluateNode(outputId, evaluated, inProgress)) {
            return false;
        }
    }

    return true;
}

} // namespace Modules

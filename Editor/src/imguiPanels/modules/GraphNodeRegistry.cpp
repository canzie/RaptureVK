#include "GraphNodeRegistry.h"

namespace Modules {

static std::string s_NodeName(NodeEntry nodeEntry)
{
    switch (nodeEntry) {
    case NODE_ADD_SCALAR:
        return "Add Scalar";
    case NODE_SUBTRACT_SCALAR:
        return "Subtract Scalar";
    case NODE_MULTIPLY_SCALAR:
        return "Multiply Scalar";
    case NODE_DIVIDE_SCALAR:
        return "Divide Scalar";
    default:
        return "UNKNOWN NODE NAME";
    }
}

void s_init()
{
    NODE_REGISTRY[NODE_ADD_SCALAR].opType = NodeOpType::ADD;
    NODE_REGISTRY[NODE_ADD_SCALAR].name = s_NodeName(NODE_ADD_SCALAR);
    NODE_REGISTRY[NODE_ADD_SCALAR].inputs.push_back(NodeParameter("A", ParameterType::F32, NodeValue(0.0f)));
    NODE_REGISTRY[NODE_ADD_SCALAR].inputs.push_back(NodeParameter("B", ParameterType::F32, NodeValue(0.0f)));
    NODE_REGISTRY[NODE_ADD_SCALAR].outputs.push_back(NodeParameter("Result", ParameterType::F32, NodeValue(0.0f)));

    NODE_REGISTRY[NODE_SUBTRACT_SCALAR].opType = NodeOpType::SUBTRACT;
    NODE_REGISTRY[NODE_SUBTRACT_SCALAR].name = s_NodeName(NODE_SUBTRACT_SCALAR);
    NODE_REGISTRY[NODE_SUBTRACT_SCALAR].inputs.push_back(NodeParameter("A", ParameterType::F32, NodeValue(0.0f)));
    NODE_REGISTRY[NODE_SUBTRACT_SCALAR].inputs.push_back(NodeParameter("B", ParameterType::F32, NodeValue(0.0f)));
    NODE_REGISTRY[NODE_SUBTRACT_SCALAR].outputs.push_back(NodeParameter("Result", ParameterType::F32, NodeValue(0.0f)));

    NODE_REGISTRY[NODE_MULTIPLY_SCALAR].opType = NodeOpType::MULTIPLY;
    NODE_REGISTRY[NODE_MULTIPLY_SCALAR].name = s_NodeName(NODE_MULTIPLY_SCALAR);
    NODE_REGISTRY[NODE_MULTIPLY_SCALAR].inputs.push_back(NodeParameter("A", ParameterType::F32, NodeValue(0.0f)));
    NODE_REGISTRY[NODE_MULTIPLY_SCALAR].inputs.push_back(NodeParameter("B", ParameterType::F32, NodeValue(0.0f)));
    NODE_REGISTRY[NODE_MULTIPLY_SCALAR].outputs.push_back(NodeParameter("Result", ParameterType::F32, NodeValue(0.0f)));

    NODE_REGISTRY[NODE_DIVIDE_SCALAR].opType = NodeOpType::DIVIDE;
    NODE_REGISTRY[NODE_DIVIDE_SCALAR].name = s_NodeName(NODE_DIVIDE_SCALAR);
    NODE_REGISTRY[NODE_DIVIDE_SCALAR].inputs.push_back(NodeParameter("A", ParameterType::F32, NodeValue(0.0f)));
    NODE_REGISTRY[NODE_DIVIDE_SCALAR].inputs.push_back(NodeParameter("B", ParameterType::F32, NodeValue(0.0f)));
    NODE_REGISTRY[NODE_DIVIDE_SCALAR].outputs.push_back(NodeParameter("Result", ParameterType::F32, NodeValue(0.0f)));
}
} // namespace Modules

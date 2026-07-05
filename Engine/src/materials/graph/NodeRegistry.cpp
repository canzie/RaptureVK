#include "NodeRegistry.h"

#include <unordered_map>

namespace Rapture {

static std::unordered_map<GraphNodeType, NodeDefinition> s_definitions;
static bool s_builtinsRegistered = false;

const char *graph_pinTypeGlsl(PinType type)
{
    switch (type) {
    case PinType::FLOAT:
        return "float";
    case PinType::INT:
        return "int";
    case PinType::VEC2:
        return "vec2";
    case PinType::VEC3:
        return "vec3";
    case PinType::VEC4:
        return "vec4";
    }
    return "vec4";
}

uint32_t graph_pinTypeComponents(PinType type)
{
    switch (type) {
    case PinType::FLOAT:
    case PinType::INT:
        return 1;
    case PinType::VEC2:
        return 2;
    case PinType::VEC3:
        return 3;
    case PinType::VEC4:
        return 4;
    }
    return 4;
}

const NodeDefinition *NodeRegistry::get(GraphNodeType type)
{
    auto it = s_definitions.find(type);
    if (it == s_definitions.end()) return nullptr;
    return &it->second;
}

void NodeRegistry::registerNode(NodeDefinition def)
{
    GraphNodeType key = def.type;
    s_definitions[key] = std::move(def);
}

void NodeRegistry::registerBuiltins()
{
    if (s_builtinsRegistered) return;
    s_builtinsRegistered = true;

    // Input readers
    registerNode({.type = GraphNodeType::TEXCOORD, .outputs = {{"out", PinType::VEC2}}, .glslTemplate = "si.uv"});
    registerNode({.type = GraphNodeType::POSITION, .outputs = {{"out", PinType::VEC3}}, .glslTemplate = "si.worldPos"});
    registerNode({.type = GraphNodeType::NORMAL, .outputs = {{"out", PinType::VEC3}}, .glslTemplate = "si.worldNormal"});

    // Slot-backed values, each reads its constant slot at the node's type
    registerNode({.type = GraphNodeType::CONSTANT_FLOAT,
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "{const}.x",
                  .resourceKind = ResourceKind::CONSTANT});
    registerNode({.type = GraphNodeType::CONSTANT_INT,
                  .outputs = {{"out", PinType::INT}},
                  .glslTemplate = "int({const}.x)",
                  .resourceKind = ResourceKind::CONSTANT});
    registerNode({.type = GraphNodeType::CONSTANT_VEC3,
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "{const}.xyz",
                  .resourceKind = ResourceKind::CONSTANT});
    registerNode({.type = GraphNodeType::CONSTANT_VEC4,
                  .outputs = {{"out", PinType::VEC4}},
                  .glslTemplate = "{const}",
                  .resourceKind = ResourceKind::CONSTANT});

    // Texture sample
    registerNode({.type = GraphNodeType::TEXTURE_SAMPLE,
                  .inputs = {{"uv", PinType::VEC2}},
                  .outputs = {{"out", PinType::VEC4}},
                  .glslTemplate = "texture(u_textures[{tex}], {uv})",
                  .resourceKind = ResourceKind::TEXTURE});

    // Multiply
    registerNode({.type = GraphNodeType::MULTIPLY_FLOAT,
                  .inputs = {{"a", PinType::FLOAT, glm::vec4(1.0f)}, {"b", PinType::FLOAT, glm::vec4(1.0f)}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "{a} * {b}"});
    registerNode({.type = GraphNodeType::MULTIPLY_VEC3,
                  .inputs = {{"a", PinType::VEC3, glm::vec4(1.0f)}, {"b", PinType::VEC3, glm::vec4(1.0f)}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "{a} * {b}"});
    registerNode({.type = GraphNodeType::MULTIPLY_INT,
                  .inputs = {{"a", PinType::INT, glm::vec4(1.0f)}, {"b", PinType::INT, glm::vec4(1.0f)}},
                  .outputs = {{"out", PinType::INT}},
                  .glslTemplate = "{a} * {b}"});

    // Add
    registerNode({.type = GraphNodeType::ADD_VEC3,
                  .inputs = {{"a", PinType::VEC3, glm::vec4(0.0f)}, {"b", PinType::VEC3, glm::vec4(0.0f)}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "{a} + {b}"});
    registerNode({.type = GraphNodeType::ADD_INT,
                  .inputs = {{"a", PinType::INT, glm::vec4(0.0f)}, {"b", PinType::INT, glm::vec4(0.0f)}},
                  .outputs = {{"out", PinType::INT}},
                  .glslTemplate = "{a} + {b}"});

    // Mix
    registerNode({.type = GraphNodeType::MIX_FLOAT,
                  .inputs = {{"a", PinType::FLOAT, glm::vec4(0.0f)},
                             {"b", PinType::FLOAT, glm::vec4(1.0f)},
                             {"t", PinType::FLOAT, glm::vec4(0.5f)}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "mix({a}, {b}, {t})"});
    registerNode({.type = GraphNodeType::MIX_VEC3,
                  .inputs = {{"a", PinType::VEC3, glm::vec4(0.0f)},
                             {"b", PinType::VEC3, glm::vec4(1.0f)},
                             {"t", PinType::FLOAT, glm::vec4(0.5f)}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "mix({a}, {b}, {t})"});

    // Fract
    registerNode({.type = GraphNodeType::FRACT_VEC3,
                  .inputs = {{"a", PinType::VEC3, glm::vec4(0.0f)}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "fract({a})"});

    // Sink
    registerNode({.type = GraphNodeType::SURFACE_OUTPUT,
                  .inputs = {{"albedo", PinType::VEC3},
                             {"normal", PinType::VEC3},
                             {"roughness", PinType::FLOAT},
                             {"metallic", PinType::FLOAT},
                             {"ao", PinType::FLOAT}}});
}

} // namespace Rapture

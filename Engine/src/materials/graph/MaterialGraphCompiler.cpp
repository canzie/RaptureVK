#include "MaterialGraphCompiler.h"

#include <cctype>
#include <cstdio>
#include <functional>
#include <string_view>
#include <unordered_set>

#include "NodeRegistry.h"
#include "textures/Texture.h"

namespace Rapture {

struct EmittedVar {
    std::string var;
    PinType type;
};

using EmittedMap = std::unordered_map<uint64_t, EmittedVar>;

static uint64_t s_emitKey(uint32_t nodeId, uint32_t pin)
{
    return (static_cast<uint64_t>(nodeId) << 32) | pin;
}

static void s_replaceAll(std::string &str, std::string_view from, std::string_view to)
{
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

static std::string s_formatFloat(float value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    std::string result = buffer;
    if (result.find('.') == std::string::npos && result.find('e') == std::string::npos && result.find('n') == std::string::npos) {
        result += ".0";
    }
    return result;
}

static std::string s_literal(const PinValue &value, PinType type)
{
    switch (type) {
    case PinType::FLOAT:
        return s_formatFloat(value.f);
    case PinType::INT:
        return std::to_string(value.i);
    case PinType::VEC2:
        return "vec2(" + s_formatFloat(value.v2.x) + ", " + s_formatFloat(value.v2.y) + ")";
    case PinType::VEC3:
        return "vec3(" + s_formatFloat(value.v3.x) + ", " + s_formatFloat(value.v3.y) + ", " + s_formatFloat(value.v3.z) + ")";
    case PinType::VEC4:
        return "vec4(" + s_formatFloat(value.v4.x) + ", " + s_formatFloat(value.v4.y) + ", " + s_formatFloat(value.v4.z) + ", " +
               s_formatFloat(value.v4.w) + ")";
    }
    return "vec4(0.0)";
}

static std::string s_coerce(std::string_view expr, PinType from, PinType to)
{
    if (from == to) return std::string(expr);

    uint32_t fromComponents = graph_pinTypeComponents(from);
    uint32_t toComponents = graph_pinTypeComponents(to);

    // A scalar source converts or splats through the target type constructor
    if (fromComponents == 1) {
        return std::string(graph_pinTypeGlsl(to)) + "(" + std::string(expr) + ")";
    }
    // A vector into a scalar takes the first channel, converting to int if needed
    if (toComponents == 1) {
        std::string channel = "(" + std::string(expr) + ").x";
        return to == PinType::INT ? "int(" + channel + ")" : channel;
    }

    if (fromComponents > toComponents) {
        return "(" + std::string(expr) + ")" + (toComponents == 2 ? ".xy" : ".xyz");
    }
    if (from == PinType::VEC2 && to == PinType::VEC3) return "vec3(" + std::string(expr) + ", 0.0)";
    if (from == PinType::VEC2 && to == PinType::VEC4) return "vec4(" + std::string(expr) + ", 0.0, 0.0)";
    if (from == PinType::VEC3 && to == PinType::VEC4) return "vec4(" + std::string(expr) + ", 1.0)";
    return std::string(expr);
}

static std::string s_sanitizeName(std::string_view name)
{
    std::string result;
    for (char c : name) {
        result += (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') ? c : '_';
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result[0])) != 0) {
        result = "g_" + result;
    }
    return result;
}

static std::string s_poolRef(std::string_view field, uint32_t slot)
{
    return "u_graphData.instances[gii]." + std::string(field) + "[" + std::to_string(slot) + "]";
}

static const GraphConnection *s_findInputConnection(const MaterialGraph &graph, uint32_t dstNode, uint32_t dstPin)
{
    for (const auto &connection : graph.connections) {
        if (connection.dstNode == dstNode && connection.dstPin == dstPin) return &connection;
    }
    return nullptr;
}

/**
 * @brief Topologically order the nodes feeding the output, sources before consumers
 * @param graph The graph to walk
 * @param order Filled with reachable node ids in dependency order
 * @return False if the graph contains a cycle
 */
static bool s_topoSort(const MaterialGraph &graph, std::vector<uint32_t> &order)
{
    std::unordered_set<uint32_t> visited;
    std::unordered_set<uint32_t> inStack;
    bool acyclic = true;

    std::function<void(uint32_t)> visit = [&](uint32_t nodeId) {
        if (visited.count(nodeId) != 0) return;
        if (inStack.count(nodeId) != 0) {
            acyclic = false;
            return;
        }
        inStack.insert(nodeId);
        for (const auto &connection : graph.connections) {
            if (connection.dstNode == nodeId) visit(connection.srcNode);
        }
        inStack.erase(nodeId);
        visited.insert(nodeId);
        order.push_back(nodeId);
    };

    visit(graph.outputNodeId);
    return acyclic;
}

/**
 * @brief Assign pool slots to resource nodes and pre-fill the instance defaults
 * @param graph The graph being compiled
 * @param order The topologically sorted node ids
 * @param defaults Pool pre-filled with each resource node's default value
 * @param mapping Node id to pool slot, for later editor writes
 * @return Empty on success, or an error message
 */
static std::string s_assignResources(const MaterialGraph &graph, const std::vector<uint32_t> &order, GraphInstanceData &defaults,
                                     GraphSlotMapping &mapping)
{
    uint32_t nextConstant = 0;
    uint32_t nextTexture = 0;
    for (uint32_t nodeId : order) {
        if (nodeId == graph.outputNodeId) continue;
        const GraphNode *node = graph.findNode(nodeId);
        const NodeDefinition *def = NodeRegistry::get(node->type);

        if (def->resourceKind == ResourceKind::CONSTANT) {
            if (nextConstant >= GRAPH_MAX_CONSTANTS) {
                return "graph uses more than " + std::to_string(GRAPH_MAX_CONSTANTS) + " constants";
            }
            uint32_t slot = nextConstant++;
            defaults.constants[slot] = node->constantValue.v4;
            mapping.constantSlots[nodeId] = slot;
        } else if (def->resourceKind == ResourceKind::TEXTURE) {
            if (nextTexture >= GRAPH_MAX_TEXTURES) {
                return "graph uses more than " + std::to_string(GRAPH_MAX_TEXTURES) + " textures";
            }
            uint32_t slot = nextTexture++;
            defaults.textures[slot] = node->texture ? node->texture->getBindlessIndex() : 0u;
            mapping.textureSlots[nodeId] = slot;
        }
    }
    return {};
}

/**
 * @brief Resolve the expression feeding one input pin, coerced to the pin type
 */
static std::string s_resolveInput(const MaterialGraph &graph, const EmittedMap &emitted, uint32_t nodeId, const PinDef &pin,
                                  uint32_t pinIndex)
{
    const GraphConnection *connection = s_findInputConnection(graph, nodeId, pinIndex);
    if (connection == nullptr) return s_literal(pin.defaultValue, pin.type);

    auto it = emitted.find(s_emitKey(connection->srcNode, connection->srcPin));
    if (it == emitted.end()) return s_literal(pin.defaultValue, pin.type);
    return s_coerce(it->second.var, it->second.type, pin.type);
}

/**
 * @brief Substitute a node definition template into its concrete GLSL expression
 */
static std::string s_emitNodeExpr(const MaterialGraph &graph, const NodeDefinition &def, const GraphNode &node,
                                  const GraphSlotMapping &mapping, const EmittedMap &emitted, std::string_view templateStr)
{
    std::string expr(templateStr);
    if (expr.empty() && def.resourceKind == ResourceKind::CONSTANT) expr = "{const}";

    for (uint32_t i = 0; i < def.inputs.size(); ++i) {
        s_replaceAll(expr, "{" + def.inputs[i].name + "}", s_resolveInput(graph, emitted, node.id, def.inputs[i], i));
    }
    if (auto it = mapping.textureSlots.find(node.id); it != mapping.textureSlots.end()) {
        s_replaceAll(expr, "{tex}", s_poolRef("textures", it->second));
    }
    if (auto it = mapping.constantSlots.find(node.id); it != mapping.constantSlots.end()) {
        s_replaceAll(expr, "{const}", s_poolRef("constants", it->second));
    }
    return expr;
}

/**
 * @brief Emit the function body, one local per node then the SurfaceData assignments
 */
static std::string s_emitSurfaceBody(const MaterialGraph &graph, const std::vector<uint32_t> &order,
                                     const GraphSlotMapping &mapping, const GraphNode &outputNode, const NodeDefinition &outputDef)
{
    EmittedMap emitted;
    std::string body;
    uint32_t counter = 0;

    // An output pin only needs a local if some connection consumes it
    std::unordered_set<uint64_t> usedOutputs;
    for (const auto &connection : graph.connections) {
        usedOutputs.insert(s_emitKey(connection.srcNode, connection.srcPin));
    }

    for (uint32_t nodeId : order) {
        if (nodeId == outputNode.id) continue;
        const GraphNode *node = graph.findNode(nodeId);
        const NodeDefinition *def = NodeRegistry::get(node->type);
        bool multiOutput = def->outputs.size() > 1;

        for (uint32_t pin = 0; pin < def->outputs.size(); ++pin) {
            if (usedOutputs.count(s_emitKey(nodeId, pin)) == 0) continue;
            const PinDef &outPin = def->outputs[pin];
            std::string_view exprTemplate = multiOutput ? std::string_view(outPin.glslTemplate) : std::string_view(def->glslTemplate);
            std::string var = "_n" + std::to_string(counter++);

            std::string expr = s_emitNodeExpr(graph, *def, *node, mapping, emitted, exprTemplate);
            body += std::string(graph_pinTypeGlsl(outPin.type)) + " " + var + "=" + expr + ";\n";
            emitted[s_emitKey(nodeId, pin)] = {std::move(var), outPin.type};
        }
    }

    auto sink = [&](std::string_view pinName, PinType type, std::string_view fallback) -> std::string {
        for (uint32_t i = 0; i < outputDef.inputs.size(); ++i) {
            if (outputDef.inputs[i].name != pinName) continue;
            const GraphConnection *connection = s_findInputConnection(graph, outputNode.id, i);
            if (connection == nullptr) break;
            auto it = emitted.find(s_emitKey(connection->srcNode, connection->srcPin));
            if (it == emitted.end()) break;
            return s_coerce(it->second.var, it->second.type, type);
        }
        return std::string(fallback);
    };

    body += "surf.albedo=" + sink("albedo", PinType::VEC3, "vec3(1.0)") + ";\n";
    body += "surf.normal=" + sink("normal", PinType::VEC3, "normalize(si.worldNormal)") + ";\n";
    body += "surf.roughness=" + sink("roughness", PinType::FLOAT, "0.5") + ";\n";
    body += "surf.metallic=" + sink("metallic", PinType::FLOAT, "0.0") + ";\n";
    body += "surf.ao=" + sink("ao", PinType::FLOAT, "1.0") + ";\n";
    body += "surf.shadingModelId=SM_OPENPBR_STANDARD;\n";
    return body;
}

CompileResult MaterialGraphCompiler::compile(const MaterialGraph &graph)
{
    CompileResult result;

    const GraphNode *outputNode = graph.findNode(graph.outputNodeId);
    if (outputNode == nullptr) {
        result.error = "graph has no output node";
        return result;
    }
    for (const auto &node : graph.nodes) {
        if (NodeRegistry::get(node.type) == nullptr) {
            result.error = "unregistered node type " + std::to_string(static_cast<int>(node.type));
            return result;
        }
    }

    std::vector<uint32_t> order;
    if (!s_topoSort(graph, order)) {
        result.error = "graph contains a cycle";
        return result;
    }

    if (std::string error = s_assignResources(graph, order, result.defaults, result.mapping); !error.empty()) {
        result.error = std::move(error);
        return result;
    }

    const NodeDefinition *outputDef = NodeRegistry::get(outputNode->type);
    std::string body = s_emitSurfaceBody(graph, order, result.mapping, *outputNode, *outputDef);

    result.functionName = "evalSurface_" + s_sanitizeName(graph.name);
    result.glslFunction = "SurfaceData " + result.functionName + "(SurfaceInputs si, uint gii){\nSurfaceData surf;\n" + body +
                          "return surf;\n}\n";
    result.success = true;
    return result;
}

} // namespace Rapture

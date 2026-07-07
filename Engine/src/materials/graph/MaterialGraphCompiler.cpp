#include "MaterialGraphCompiler.h"

#include <cctype>
#include <cstdio>
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
using DiagnosticList = std::vector<MaterialCompilerDiagnostic>;

static uint64_t s_emitKey(uint32_t nodeId, uint32_t pin)
{
    return (static_cast<uint64_t>(nodeId) << 32) | pin;
}

static void s_addDiagnostic(DiagnosticList &diagnostics, MaterialCompilerDiagnosticLevel level, std::string message,
                            uint32_t nodeId)
{
    diagnostics.push_back({level, std::move(message), nodeId});
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
 *
 * Iterative post-order DFS from the output. A node is colored gray while its dependencies are on
 * the stack and black once emitted; meeting a gray dependency is a back edge, so a cycle.
 */
static bool s_topoSort(const MaterialGraph &graph, std::vector<uint32_t> &order)
{
    // Each node's dependencies are the sources feeding any of its inputs
    std::unordered_map<uint32_t, std::vector<uint32_t>> dependencies;
    for (const auto &connection : graph.connections) {
        dependencies[connection.dstNode].push_back(connection.srcNode);
    }

    enum Color : uint8_t { WHITE = 0, GRAY, BLACK };
    std::unordered_map<uint32_t, Color> color;
    bool acyclic = true;

    std::vector<uint32_t> stack;
    stack.push_back(graph.outputNodeId);
    while (!stack.empty()) {
        uint32_t nodeId = stack.back();
        Color state = color[nodeId];
        if (state == WHITE) {
            color[nodeId] = GRAY;
            auto it = dependencies.find(nodeId);
            if (it != dependencies.end()) {
                for (uint32_t dependency : it->second) {
                    Color depState = color[dependency];
                    if (depState == GRAY) {
                        acyclic = false;
                    } else if (depState == WHITE) {
                        stack.push_back(dependency);
                    }
                }
            }
        } else if (state == GRAY) {
            color[nodeId] = BLACK;
            order.push_back(nodeId);
            stack.pop_back();
        } else {
            stack.pop_back();
        }
    }
    return acyclic;
}

/**
 * @brief Emit an info diagnostic for every node that does not feed the output
 */
static void s_reportDeadNodes(const MaterialGraph &graph, const std::vector<uint32_t> &order, DiagnosticList &diagnostics)
{
    std::unordered_set<uint32_t> reachable(order.begin(), order.end());
    for (const auto &node : graph.nodes) {
        if (reachable.count(node.id) == 0) {
            s_addDiagnostic(diagnostics, MaterialCompilerDiagnosticLevel::INFO,
                            "node does not affect the output and was eliminated", node.id);
        }
    }
}

/**
 * @brief Structural checks that must pass before codegen, gathering all failures as diagnostics
 * @param graph The graph to validate
 * @param outputNode The resolved output node, or nullptr if the id did not resolve
 * @param diagnostics Appended with any problems found
 */
static void s_validateGraph(const MaterialGraph &graph, const GraphNode *outputNode, DiagnosticList &diagnostics)
{
    using Level = MaterialCompilerDiagnosticLevel;

    if (outputNode == nullptr) {
        s_addDiagnostic(diagnostics, Level::ERROR, "graph has no output node with id " + std::to_string(graph.outputNodeId),
                        UINT32_MAX);
        return; // nothing downstream is meaningful without the sink
    }
    if (outputNode->type != GraphNodeType::SURFACE_OUTPUT) {
        s_addDiagnostic(diagnostics, Level::ERROR, "the output node is not a SURFACE_OUTPUT node", outputNode->id);
    }

    for (const auto &node : graph.nodes) {
        if (NodeRegistry::get(node.type) == nullptr) {
            s_addDiagnostic(diagnostics, Level::ERROR,
                            "node uses an unregistered type " + std::to_string(static_cast<int>(node.type)), node.id);
        }
    }

    std::unordered_set<uint64_t> wiredInputs;
    for (const auto &connection : graph.connections) {
        const GraphNode *src = graph.findNode(connection.srcNode);
        const GraphNode *dst = graph.findNode(connection.dstNode);
        if (src == nullptr) {
            s_addDiagnostic(diagnostics, Level::ERROR,
                            "connection references a missing source node " + std::to_string(connection.srcNode),
                            connection.dstNode);
            continue;
        }
        if (dst == nullptr) {
            s_addDiagnostic(diagnostics, Level::ERROR,
                            "connection references a missing destination node " + std::to_string(connection.dstNode),
                            connection.srcNode);
            continue;
        }

        const NodeDefinition *srcDef = NodeRegistry::get(src->type);
        const NodeDefinition *dstDef = NodeRegistry::get(dst->type);
        if (srcDef == nullptr || dstDef == nullptr) continue; // unregistered type already reported

        if (connection.srcPin >= srcDef->outputs.size()) {
            s_addDiagnostic(diagnostics, Level::ERROR,
                            "connection uses output pin " + std::to_string(connection.srcPin) + " but the node has " +
                                std::to_string(srcDef->outputs.size()),
                            connection.srcNode);
        }
        if (connection.dstPin >= dstDef->inputs.size()) {
            s_addDiagnostic(diagnostics, Level::ERROR,
                            "connection uses input pin " + std::to_string(connection.dstPin) + " but the node has " +
                                std::to_string(dstDef->inputs.size()),
                            connection.dstNode);
            continue;
        }

        if (!wiredInputs.insert(s_emitKey(connection.dstNode, connection.dstPin)).second) {
            s_addDiagnostic(diagnostics, Level::ERROR,
                            "input pin " + std::to_string(connection.dstPin) + " has more than one incoming connection",
                            connection.dstNode);
        }
    }
}

/**
 * @brief Assign pool slots to resource nodes and pre-fill the instance defaults
 * @param graph The graph being compiled
 * @param order The topologically sorted node ids
 * @param defaults Pool pre-filled with each resource node's default value
 * @param mapping Node id to pool slot, for later editor writes
 * @param diagnostics Appended with an error if either pool overflows
 * @return False if a pool overflowed, in which case an error diagnostic was added
 */
static bool s_assignResources(const MaterialGraph &graph, const std::vector<uint32_t> &order, GraphInstanceData &defaults,
                              GraphSlotMapping &mapping, DiagnosticList &diagnostics)
{
    uint32_t nextConstant = 0;
    uint32_t nextTexture = 0;
    for (uint32_t nodeId : order) {
        if (nodeId == graph.outputNodeId) continue;
        const GraphNode *node = graph.findNode(nodeId);
        const NodeDefinition *def = NodeRegistry::get(node->type);

        if (def->resourceKind == ResourceKind::CONSTANT) {
            if (nextConstant >= GRAPH_MAX_CONSTANTS) {
                s_addDiagnostic(diagnostics, MaterialCompilerDiagnosticLevel::ERROR,
                                "graph uses more than " + std::to_string(GRAPH_MAX_CONSTANTS) + " constants", nodeId);
                return false;
            }
            uint32_t slot = nextConstant++;
            defaults.constants[slot] = node->constantValue.v4;
            mapping.constantSlots[nodeId] = slot;
        } else if (def->resourceKind == ResourceKind::TEXTURE) {
            if (nextTexture >= GRAPH_MAX_TEXTURES) {
                s_addDiagnostic(diagnostics, MaterialCompilerDiagnosticLevel::ERROR,
                                "graph uses more than " + std::to_string(GRAPH_MAX_TEXTURES) + " textures", nodeId);
                return false;
            }
            uint32_t slot = nextTexture++;
            defaults.textures[slot] = node->texture ? node->texture->getBindlessIndex() : 0u;
            mapping.textureSlots[nodeId] = slot;
        }
    }
    return true;
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

CompileResult MaterialGraphCompiler::compile(const MaterialGraph &graph, uint32_t graphId)
{
    CompileResult result;
    result.graphId = graphId;

    const GraphNode *outputNode = graph.findNode(graph.outputNodeId);
    s_validateGraph(graph, outputNode, result.diagnostics);
    if (result.hasErrors()) return result;

    std::vector<uint32_t> order;
    if (!s_topoSort(graph, order)) {
        s_addDiagnostic(result.diagnostics, MaterialCompilerDiagnosticLevel::ERROR, "graph contains a cycle", UINT32_MAX);
        return result;
    }
    s_reportDeadNodes(graph, order, result.diagnostics);

    if (!s_assignResources(graph, order, result.defaults, result.mapping, result.diagnostics)) {
        return result;
    }

    const NodeDefinition *outputDef = NodeRegistry::get(outputNode->type);
    std::string body = s_emitSurfaceBody(graph, order, result.mapping, *outputNode, *outputDef);

    // graphId keeps the name unique even when two graphs share a sanitized name
    result.functionName = "evalSurface_" + s_sanitizeName(graph.name) + "_" + std::to_string(graphId);
    result.glslFunction = "SurfaceData " + result.functionName + "(SurfaceInputs si, uint gii){\nSurfaceData surf;\n" + body +
                          "return surf;\n}\n";
    result.success = true;
    return result;
}

} // namespace Rapture

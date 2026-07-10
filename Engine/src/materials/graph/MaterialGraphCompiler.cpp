#include "MaterialGraphCompiler.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>
#include <unordered_set>

#include "NodeRegistry.h"
#include "logging/Log.h"
#include "materials/MaterialData.h"
#include "textures/Texture.h"
#include "utils/rp_assert.h"

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
    case PinType::TEXTURE:
        RP_CORE_ERROR("Graph compiler asked for a numeric literal of a texture pin, textures carry no literal");
        break;
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

// A single raw uint of the instance slice, read as u_graphPool.data[base + offset]
static std::string s_poolElem(GraphBufferOffset offset)
{
    return "u_graphPool.data[base + " + std::to_string(offset) + "]";
}

// The instance slice value at offset, reconstructed at its pin type from the packed uints
static std::string s_poolRead(GraphBufferOffset offset, PinType type)
{
    switch (type) {
    case PinType::FLOAT:
        return "uintBitsToFloat(" + s_poolElem(offset) + ")";
    case PinType::INT:
        return "int(" + s_poolElem(offset) + ")";
    case PinType::VEC2:
        return "vec2(uintBitsToFloat(" + s_poolElem(offset) + "), uintBitsToFloat(" + s_poolElem(offset + 1) + "))";
    case PinType::VEC3:
        return "vec3(uintBitsToFloat(" + s_poolElem(offset) + "), uintBitsToFloat(" + s_poolElem(offset + 1) +
               "), uintBitsToFloat(" + s_poolElem(offset + 2) + "))";
    case PinType::VEC4:
        return "vec4(uintBitsToFloat(" + s_poolElem(offset) + "), uintBitsToFloat(" + s_poolElem(offset + 1) +
               "), uintBitsToFloat(" + s_poolElem(offset + 2) + "), uintBitsToFloat(" + s_poolElem(offset + 3) + "))";
    case PinType::TEXTURE:
        return s_poolElem(offset);
    }
    return "0.0";
}

// Write a value's component bit patterns into the slice, growing it to fit
static void s_packValue(GraphInstanceData &slice, GraphBufferOffset offset, const PinValue &value, PinType type)
{
    uint32_t components = graph_pinTypeComponents(type);
    if (slice.size() < offset + components) slice.resize(offset + components, 0u);

    if (type == PinType::INT) {
        slice[offset] = static_cast<uint32_t>(value.i);
        return;
    }
    for (uint32_t component = 0; component < components; ++component) {
        float scalar = (&value.v4.x)[component];
        std::memcpy(&slice[offset + component], &scalar, sizeof(float));
    }
}

static const GraphConnection *s_findInputConnection(const MaterialGraph &graph, uint32_t dstNode, uint32_t dstPin)
{
    for (const auto &connection : graph.connections) {
        if (connection.dstNode == dstNode && connection.dstPin == dstPin) return &connection;
    }
    return nullptr;
}

/**
 * @brief Index of the SURFACE_OUTPUT input pin with a name
 * @param outputDef The sink node definition
 * @param name The field name to match
 * @return The pin index, or -1 if the sink has no such pin
 */
static int s_findSinkPin(const NodeDefinition &outputDef, std::string_view name)
{
    for (uint32_t i = 0; i < outputDef.inputs.size(); ++i) {
        if (outputDef.inputs[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

/**
 * @brief The source nodes feeding the sink pins a variant's fields drive, its topo sort roots
 * @param graph The graph
 * @param outputNodeId The sink node id
 * @param outputDef The sink node definition
 * @param fields The variant's surface output fields
 * @param fieldCount Number of fields
 * @return The source node ids feeding those pins
 */
static std::vector<uint32_t> s_sinkRoots(const MaterialGraph &graph, uint32_t outputNodeId, const NodeDefinition &outputDef,
                                         const SurfaceOutputField *fields, size_t fieldCount)
{
    std::unordered_set<uint32_t> pins;
    for (size_t i = 0; i < fieldCount; ++i) {
        int pin = s_findSinkPin(outputDef, fields[i].name);
        if (pin >= 0) pins.insert(static_cast<uint32_t>(pin));
    }

    std::vector<uint32_t> roots;
    for (const auto &connection : graph.connections) {
        if (connection.dstNode == outputNodeId && pins.count(connection.dstPin) != 0) {
            roots.push_back(connection.srcNode);
        }
    }
    return roots;
}

/**
 * @brief Topologically order the nodes feeding the given roots, sources before consumers
 * @param graph The graph to walk
 * @param roots The nodes to walk back from
 * @param order Filled with reachable node ids in dependency order
 * @return False if the graph contains a cycle
 *
 * Iterative post-order DFS from each root. A node is colored gray while its dependencies are on the
 * stack and black once emitted; meeting a gray dependency is a back edge, so a cycle.
 */
static bool s_topoSort(const MaterialGraph &graph, const std::vector<uint32_t> &roots, std::vector<uint32_t> &order)
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
    for (uint32_t root : roots) {
        if (color[root] != WHITE) {
            continue;
        }
        stack.push_back(root);
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

    // A texture pin has no literal fallback, so a required one must be wired or authored
    for (const auto &node : graph.nodes) {
        const NodeDefinition *def = NodeRegistry::get(node.type);
        if (def == nullptr) {
            continue;
        }
        for (uint32_t i = 0; i < def->inputs.size(); ++i) {
            if (def->inputs[i].type != PinType::TEXTURE) {
                continue;
            }
            bool wired = wiredInputs.count(s_emitKey(node.id, i)) != 0;
            bool authored = i < node.inputTextures.size() && static_cast<bool>(node.inputTextures[i]);
            if (!wired && !authored) {
                s_addDiagnostic(diagnostics, Level::ERROR,
                                "input pin '" + def->inputs[i].name + "' requires a texture but none is connected or set",
                                node.id);
            }
        }
    }
}

/**
 * @brief Pack each node's authored, unwired input pin values into the instance slice
 * @param graph The graph being compiled
 * @param order The topologically sorted node ids
 * @param defaults The slice, grown and pre-filled with each authored pin value
 * @param mapping Records where each authored pin landed, for later editor writes
 * @param textureRefs Collects every texture the slice indexes so it stays resident
 *
 * A wired pin draws its value from upstream and an unauthored pin bakes its literal default, so
 * only an unwired pin carrying an authored value reserves a slot.
 */
static void s_assignResources(const MaterialGraph &graph, const std::vector<uint32_t> &order, GraphInstanceData &defaults,
                              GraphSlotMapping &mapping, std::vector<AssetPtr<Texture>> &textureRefs)
{
    GraphBufferOffset next = 0;
    for (uint32_t nodeId : order) {
        const GraphNode *node = graph.findNode(nodeId);
        const NodeDefinition *def = NodeRegistry::get(node->type);
        if (def == nullptr) {
            continue;
        }

        for (uint32_t i = 0; i < def->inputs.size(); ++i) {
            if (s_findInputConnection(graph, nodeId, i) != nullptr) {
                continue;
            }

            const PinDef &pin = def->inputs[i];
            if (pin.type == PinType::TEXTURE) {
                AssetPtr<Texture> texture = i < node->inputTextures.size() ? node->inputTextures[i] : AssetPtr<Texture>{};
                if (!texture) {
                    continue;
                }

                GraphBufferOffset offset = next++;
                if (defaults.size() <= offset) {
                    defaults.resize(offset + 1, 0u);
                }
                defaults[offset] = texture->getBindlessIndex();
                mapping.slots[Graph_pinKey(nodeId, i)] = {offset, PinType::TEXTURE};
                textureRefs.push_back(std::move(texture));
            } else {
                if (i >= node->inputValues.size() || !node->inputValues[i].has_value()) {
                    continue;
                }

                GraphBufferOffset offset = next;
                next += graph_pinTypeComponents(pin.type);
                s_packValue(defaults, offset, *node->inputValues[i], pin.type);
                mapping.slots[Graph_pinKey(nodeId, i)] = {offset, pin.type};
            }
        }
    }
}

/**
 * @brief Resolve the expression feeding one input pin, coerced to the pin type
 *
 * A wired pin uses its upstream local; an unwired pin reads its authored slot from the instance
 * slice, or bakes its literal default when nothing was authored.
 */
static std::string s_resolveInput(const MaterialGraph &graph, const GraphSlotMapping &mapping, const EmittedMap &emitted,
                                  uint32_t nodeId, const PinDef &pin, uint32_t pinIndex)
{
    const GraphConnection *connection = s_findInputConnection(graph, nodeId, pinIndex);
    if (connection != nullptr) {
        auto it = emitted.find(s_emitKey(connection->srcNode, connection->srcPin));
        if (it != emitted.end()) {
            return s_coerce(it->second.var, it->second.type, pin.type);
        }
    }

    auto slot = mapping.slots.find(Graph_pinKey(nodeId, pinIndex));
    if (pin.type == PinType::TEXTURE) {
        RP_ASSERT(slot != mapping.slots.end(), "texture pin reached codegen without a slot, validation should have rejected it");
        return s_poolElem(slot->second.offset);
    }
    if (slot != mapping.slots.end()) {
        return s_poolRead(slot->second.offset, slot->second.type);
    }
    return s_literal(pin.defaultValue, pin.type);
}

/**
 * @brief Substitute a node definition template into its concrete GLSL expression
 */
static std::string s_emitNodeExpr(const MaterialGraph &graph, const NodeDefinition &def, const GraphNode &node,
                                  const GraphSlotMapping &mapping, const EmittedMap &emitted, std::string_view templateStr)
{
    std::string expr(templateStr);
    for (uint32_t i = 0; i < def.inputs.size(); ++i) {
        s_replaceAll(expr, "{" + def.inputs[i].name + "}", s_resolveInput(graph, mapping, emitted, node.id, def.inputs[i], i));
    }
    return expr;
}

/**
 * @brief Emit the function body, one local per node then the surface field assignments
 * @param fields The chosen variant's surface output fields, mirroring the GLSL struct
 * @param fieldCount Number of fields
 */
static std::string s_emitSurfaceBody(const MaterialGraph &graph, const std::vector<uint32_t> &order,
                                     const GraphSlotMapping &mapping, const GraphNode &outputNode, const NodeDefinition &outputDef,
                                     const SurfaceOutputField *fields, size_t fieldCount)
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

    for (size_t f = 0; f < fieldCount; ++f) {
        const SurfaceOutputField &field = fields[f];
        std::string value = field.fallback;

        int pinIndex = s_findSinkPin(outputDef, field.name);
        if (pinIndex >= 0) {
            uint32_t pin = static_cast<uint32_t>(pinIndex);
            PinType fieldType = outputDef.inputs[pin].type;
            const GraphConnection *connection = s_findInputConnection(graph, outputNode.id, pin);
            if (connection != nullptr) {
                auto it = emitted.find(s_emitKey(connection->srcNode, connection->srcPin));
                if (it != emitted.end()) value = s_coerce(it->second.var, it->second.type, fieldType);
            } else {
                auto slot = mapping.slots.find(Graph_pinKey(outputNode.id, pin));
                if (slot != mapping.slots.end()) {
                    value = s_coerce(s_poolRead(slot->second.offset, slot->second.type), slot->second.type, fieldType);
                }
            }
        }
        body += "surf." + std::string(field.name) + "=" + value + ";\n";
    }
    return body;
}

CompileResult MaterialGraphCompiler::compile(const MaterialGraph &graph, uint32_t graphId)
{
    CompileResult result;
    result.graphId = graphId;

    const GraphNode *outputNode = graph.findNode(graph.outputNodeId);
    s_validateGraph(graph, outputNode, result.diagnostics);
    if (result.hasErrors()) return result;

    const NodeDefinition *outputDef = NodeRegistry::get(outputNode->type);

    // The G-buffer variant walks the whole graph from the sink; the output node itself stays in the
    // order so dead-node reporting does not flag it
    std::vector<uint32_t> gbufferOrder;
    if (!s_topoSort(graph, {graph.outputNodeId}, gbufferOrder)) {
        s_addDiagnostic(result.diagnostics, MaterialCompilerDiagnosticLevel::ERROR, "graph contains a cycle", UINT32_MAX);
        return result;
    }
    s_reportDeadNodes(graph, gbufferOrder, result.diagnostics);

    s_assignResources(graph, gbufferOrder, result.defaults, result.mapping, result.textureRefs);

    // graphId keeps the name unique even when two graphs share a sanitized name
    std::string name = s_sanitizeName(graph.name) + "_" + std::to_string(graphId);

    std::string gbufferBody = s_emitSurfaceBody(graph, gbufferOrder, result.mapping, *outputNode, *outputDef, SURFACE_DATA_FIELDS,
                                                std::size(SURFACE_DATA_FIELDS));
    std::string gbufferName = "evalSurface_" + name;
    result.functions.push_back({SurfaceVariant::GBUFFER, gbufferName,
                                "SurfaceData " + gbufferName + "(SurfaceInputs si, uint base){\nSurfaceData surf;\n" + gbufferBody +
                                    "return surf;\n}\n"});

    // The diffuse variant seeds only its own fields, so branches feeding roughness, metallic or ao
    // prune away and the emit shares the slice layout assigned above
    std::vector<uint32_t> diffuseRoots =
        s_sinkRoots(graph, outputNode->id, *outputDef, SURFACE_DATA_DIFFUSE_FIELDS, std::size(SURFACE_DATA_DIFFUSE_FIELDS));
    std::vector<uint32_t> diffuseOrder;
    s_topoSort(graph, diffuseRoots, diffuseOrder);
    std::string diffuseBody = s_emitSurfaceBody(graph, diffuseOrder, result.mapping, *outputNode, *outputDef,
                                                SURFACE_DATA_DIFFUSE_FIELDS, std::size(SURFACE_DATA_DIFFUSE_FIELDS));
    std::string diffuseName = "evalSurfaceDiffuse_" + name;
    result.functions.push_back({SurfaceVariant::DIFFUSE, diffuseName,
                                "SurfaceDataDiffuse " + diffuseName + "(SurfaceInputs si, uint base){\nSurfaceDataDiffuse surf;\n" +
                                    diffuseBody + "return surf;\n}\n"});

    result.success = true;
    return result;
}

} // namespace Rapture

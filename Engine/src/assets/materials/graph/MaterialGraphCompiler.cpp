#include "MaterialGraphCompiler.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "NodeRegistry.h"
#include "core/utils/Log.h"
#include "gpu/textures/Texture.h"
#include "core/utils/rp_assert.h"

namespace Rapture {

using DiagnosticList = std::vector<MaterialCompilerDiagnostic>;

// One emitted GLSL local standing in for a node output pin, tracked so consumers can reference it
struct EmittedVar {
    std::string var;
    PinType type;
};
using EmittedMap = std::unordered_map<GraphPinKey, EmittedVar>;

/**
 * @brief Indexed, resolved view of a graph, built once and shared by every compile phase
 *
 * Validation, resource assignment and each pass emit read these maps instead of re-scanning the
 * node and connection lists, so the compiler is linear in the graph size rather than quadratic.
 */
struct GraphContext {
    const MaterialGraph &graph;
    const GraphDomain &domain;
    const GraphNode &sink;
    const NodeDefinition &sinkDef;

    std::unordered_map<uint32_t, const GraphNode *> nodeById = {};       // node id -> node
    std::unordered_map<uint32_t, const NodeDefinition *> defById = {};   // node id -> its definition
    std::unordered_map<GraphPinKey, const GraphConnection *> wires = {}; // (dstNode,dstPin) -> incoming wire
    std::unordered_map<uint32_t, std::vector<uint32_t>> deps = {};       // node id -> its source node ids
    std::unordered_set<GraphPinKey> usedOutputs = {};                    // (srcNode,srcPin) some wire consumes

    /**
     * @brief The node with an id, or nullptr when the graph has no such node
     */
    const GraphNode *node(uint32_t id) const
    {
        auto it = nodeById.find(id);
        return it != nodeById.end() ? it->second : nullptr;
    }

    /**
     * @brief The wire feeding an input pin, or nullptr when it is unconnected
     */
    const GraphConnection *wireInto(uint32_t node, uint32_t pin) const
    {
        auto it = wires.find(Graph_pinKey(node, pin));
        return it != wires.end() ? it->second : nullptr;
    }
};

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

/**
 * @brief Index the graph into a GraphContext, resolving definitions, wires, dependencies and uses
 */
static void s_buildContext(GraphContext &ctx)
{
    for (const auto &node : ctx.graph.nodes) {
        ctx.nodeById[node.id] = &node;
        ctx.defById[node.id] = NodeRegistry::get(node.type);
    }
    for (const auto &connection : ctx.graph.connections) {
        ctx.wires[Graph_pinKey(connection.dstNode, connection.dstPin)] = &connection;
        ctx.deps[connection.dstNode].push_back(connection.srcNode);
        ctx.usedOutputs.insert(Graph_pinKey(connection.srcNode, connection.srcPin));
    }
}

/**
 * @brief Structural checks that must pass before codegen, gathering every failure as a diagnostic
 */
static void s_validate(const GraphContext &ctx, DiagnosticList &diagnostics)
{
    using Level = MaterialCompilerDiagnosticLevel;

    for (const auto &node : ctx.graph.nodes) {
        if (ctx.defById.at(node.id) == nullptr) {
            s_addDiagnostic(diagnostics, Level::ERROR,
                            "node uses an unregistered type " + std::to_string(static_cast<int>(node.type)), node.id);
        }
    }

    std::unordered_set<GraphPinKey> wiredInputs;
    for (const auto &connection : ctx.graph.connections) {
        const GraphNode *src = ctx.node(connection.srcNode);
        const GraphNode *dst = ctx.node(connection.dstNode);
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

        const NodeDefinition *srcDef = ctx.defById.at(connection.srcNode);
        const NodeDefinition *dstDef = ctx.defById.at(connection.dstNode);
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

        if (!wiredInputs.insert(Graph_pinKey(connection.dstNode, connection.dstPin)).second) {
            s_addDiagnostic(diagnostics, Level::ERROR,
                            "input pin " + std::to_string(connection.dstPin) + " has more than one incoming connection",
                            connection.dstNode);
        }
    }

    for (const auto &node : ctx.graph.nodes) {
        const NodeDefinition *def = ctx.defById.at(node.id);
        if (def == nullptr) {
            continue; // unregistered type already reported
        }

        // A node can only read the inputs its domain provides
        std::string_view missing;
        if (!Graph_nodeFitsDomain(*def, ctx.domain, &missing)) {
            s_addDiagnostic(diagnostics, Level::ERROR,
                            "node type " + std::string(Graph_nodeTypeName(node.type)) + " reads the input '" +
                                std::string(missing) + "', which the " + Graph_domainName(ctx.domain.id) +
                                " domain does not provide",
                            node.id);
        }

        // A texture pin has no literal fallback, so a required one must be wired or authored
        for (uint32_t i = 0; i < def->inputs.size(); ++i) {
            if (def->inputs[i].type != PinType::TEXTURE) {
                continue;
            }
            bool wired = ctx.wireInto(node.id, i) != nullptr;
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
 * @brief Topologically order the nodes feeding the given roots, sources before consumers
 * @param ctx The indexed graph
 * @param roots The nodes to walk back from
 * @param order Filled with reachable node ids in dependency order
 * @return False if the graph contains a cycle
 *
 * Iterative post-order DFS from each root over ctx.deps. A node is gray while its dependencies are
 * on the stack and black once emitted; meeting a gray dependency is a back edge, so a cycle.
 */
static bool s_topoSort(const GraphContext &ctx, const std::vector<uint32_t> &roots, std::vector<uint32_t> &order)
{
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
                auto it = ctx.deps.find(nodeId);
                if (it != ctx.deps.end()) {
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
static void s_reportDeadNodes(const GraphContext &ctx, const std::vector<uint32_t> &order, DiagnosticList &diagnostics)
{
    std::unordered_set<uint32_t> reachable(order.begin(), order.end());
    for (const auto &node : ctx.graph.nodes) {
        if (reachable.count(node.id) == 0) {
            s_addDiagnostic(diagnostics, MaterialCompilerDiagnosticLevel::INFO,
                            "node does not affect the output and was eliminated", node.id);
        }
    }
}

/**
 * @brief Pack each node's authored, unwired input pin values into the shared instance slice
 *
 * A wired pin draws its value from upstream and an unauthored pin bakes its literal default, so only
 * an unwired pin carrying an authored value reserves a slot. The layout is shared by every pass.
 */
static void s_assignResources(const GraphContext &ctx, const std::vector<uint32_t> &order, GraphInstanceData &defaults,
                              GraphSlotMapping &mapping, std::vector<AssetPtr<Texture>> &textureRefs)
{
    GraphBufferOffset next = 0;
    for (uint32_t nodeId : order) {
        const GraphNode *node = ctx.node(nodeId);
        const NodeDefinition *def = ctx.defById.at(nodeId);
        if (def == nullptr) {
            continue;
        }

        for (uint32_t i = 0; i < def->inputs.size(); ++i) {
            if (ctx.wireInto(nodeId, i) != nullptr) {
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
static std::string s_resolveInput(const GraphContext &ctx, const GraphSlotMapping &mapping, const EmittedMap &emitted,
                                  uint32_t nodeId, const PinDef &pin, uint32_t pinIndex)
{
    if (const GraphConnection *wire = ctx.wireInto(nodeId, pinIndex)) {
        auto it = emitted.find(Graph_pinKey(wire->srcNode, wire->srcPin));
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
 *
 * A {pin} placeholder resolves to an upstream local or a slice read, a {$name} placeholder to the
 * domain's expression for that input.
 */
static std::string s_emitNodeExpr(const GraphContext &ctx, const NodeDefinition &def, const GraphNode &node,
                                  const GraphSlotMapping &mapping, const EmittedMap &emitted, std::string_view templateStr)
{
    std::string expr(templateStr);
    for (uint32_t i = 0; i < def.inputs.size(); ++i) {
        s_replaceAll(expr, "{" + def.inputs[i].name + "}", s_resolveInput(ctx, mapping, emitted, node.id, def.inputs[i], i));
    }
    Graph_substituteDomainInputs(expr, ctx.domain);
    return expr;
}

/**
 * @brief Emit one local per live node output, in dependency order, into emitted
 */
static std::string s_emitLocals(const GraphContext &ctx, const std::vector<uint32_t> &order, const GraphSlotMapping &mapping,
                                EmittedMap &emitted)
{
    std::string body;
    uint32_t counter = 0;
    for (uint32_t nodeId : order) {
        if (nodeId == ctx.sink.id) continue;
        const GraphNode *node = ctx.node(nodeId);
        const NodeDefinition *def = ctx.defById.at(nodeId);
        bool multiOutput = def->outputs.size() > 1;

        for (uint32_t pin = 0; pin < def->outputs.size(); ++pin) {
            if (ctx.usedOutputs.count(Graph_pinKey(nodeId, pin)) == 0) continue;
            const PinDef &outPin = def->outputs[pin];
            std::string_view exprTemplate = multiOutput ? std::string_view(outPin.glslTemplate) : std::string_view(def->glslTemplate);
            std::string var = "_n" + std::to_string(counter++);

            std::string expr = s_emitNodeExpr(ctx, *def, *node, mapping, emitted, exprTemplate);
            body += std::string(graph_pinTypeGlsl(outPin.type)) + " " + var + "=" + expr + ";\n";
            emitted[Graph_pinKey(nodeId, pin)] = {std::move(var), outPin.type};
        }
    }
    return body;
}

/**
 * @brief The sink input pin a field binds to, or -1 for a constant field or an absent pin
 */
static int s_fieldPin(const NodeDefinition &sinkDef, const GraphOutputField &field)
{
    if (field.constant) {
        return -1;
    }
    for (uint32_t i = 0; i < sinkDef.inputs.size(); ++i) {
        if (sinkDef.inputs[i].name == field.name) return static_cast<int>(i);
    }
    return -1;
}

/**
 * @brief A field's fallback with its domain input references bound
 */
static std::string s_fallbackExpr(const GraphContext &ctx, const GraphOutputField &field)
{
    std::string expr(field.fallback);
    Graph_substituteDomainInputs(expr, ctx.domain);
    return expr;
}

/**
 * @brief The value expression for one output field: its bound pin, its authored slot, or its fallback
 */
static std::string s_emitFieldValue(const GraphContext &ctx, const GraphOutputField &field, const GraphSlotMapping &mapping,
                                    const EmittedMap &emitted)
{
    int pinIndex = s_fieldPin(ctx.sinkDef, field);
    if (pinIndex < 0) {
        return s_fallbackExpr(ctx, field);
    }

    uint32_t pin = static_cast<uint32_t>(pinIndex);
    if (const GraphConnection *wire = ctx.wireInto(ctx.sink.id, pin)) {
        auto it = emitted.find(Graph_pinKey(wire->srcNode, wire->srcPin));
        if (it != emitted.end()) {
            return s_coerce(it->second.var, it->second.type, field.type);
        }
    }

    auto slot = mapping.slots.find(Graph_pinKey(ctx.sink.id, pin));
    if (slot != mapping.slots.end()) {
        return s_coerce(s_poolRead(slot->second.offset, slot->second.type), slot->second.type, field.type);
    }
    return s_fallbackExpr(ctx, field);
}

/**
 * @brief The nodes feeding the sink pins this pass's fields drive, its topo roots
 */
static std::vector<uint32_t> s_passRoots(const GraphContext &ctx, const GraphPass &pass)
{
    std::vector<uint32_t> roots;
    for (const auto &field : pass.fields) {
        int pinIndex = s_fieldPin(ctx.sinkDef, field);
        if (pinIndex < 0) {
            continue;
        }
        if (const GraphConnection *wire = ctx.wireInto(ctx.sink.id, static_cast<uint32_t>(pinIndex))) {
            roots.push_back(wire->srcNode);
        }
    }
    return roots;
}

/**
 * @brief Compile one pass to its GLSL function: DCE from the pass roots, locals, then field writes
 */
static CompiledFunction s_emitPass(const GraphContext &ctx, const GraphPass &pass, size_t passIndex,
                                   const std::string &name, const GraphSlotMapping &mapping)
{
    std::vector<uint32_t> order;
    s_topoSort(ctx, s_passRoots(ctx, pass), order);

    EmittedMap emitted;
    std::string body = s_emitLocals(ctx, order, mapping, emitted);
    for (const auto &field : pass.fields) {
        body += "surf." + std::string(field.name) + "=" + s_emitFieldValue(ctx, field, mapping, emitted) + ";\n";
    }

    CompiledFunction function;
    function.passIndex = passIndex;
    function.functionName = std::string(pass.funcPrefix) + name;
    function.glslFunction = std::string(pass.structName) + " " + function.functionName + "(" +
                            std::string(ctx.domain.inputStructName) + " si, uint base){\n" + std::string(pass.structName) +
                            " surf;\n" + body + "return surf;\n}\n";
    return function;
}

CompileResult MaterialGraphCompiler::compile(const MaterialGraph &graph, uint32_t graphId)
{
    CompileResult result;
    result.graphId = graphId;

    const GraphDomain *domain = GraphDomainRegistry::forId(graph.domain);
    if (domain == nullptr) {
        s_addDiagnostic(result.diagnostics, MaterialCompilerDiagnosticLevel::ERROR,
                        "graph names the domain " + std::string(Graph_domainName(graph.domain)) + ", which is not registered",
                        UINT32_MAX);
        return result;
    }

    const GraphNode *sink = graph.findNode(graph.outputNodeId);
    if (sink == nullptr) {
        s_addDiagnostic(result.diagnostics, MaterialCompilerDiagnosticLevel::ERROR,
                        "graph has no output node with id " + std::to_string(graph.outputNodeId), UINT32_MAX);
        return result;
    }
    if (sink->type != domain->sinkType) {
        s_addDiagnostic(result.diagnostics, MaterialCompilerDiagnosticLevel::ERROR,
                        "graph output node is a " + std::string(Graph_nodeTypeName(sink->type)) + " but the " +
                            Graph_domainName(domain->id) + " domain sinks into a " +
                            std::string(Graph_nodeTypeName(domain->sinkType)),
                        sink->id);
        return result;
    }
    const NodeDefinition *sinkDef = NodeRegistry::get(sink->type);
    if (sinkDef == nullptr) {
        s_addDiagnostic(result.diagnostics, MaterialCompilerDiagnosticLevel::ERROR,
                        "the output node type has no registered definition", sink->id);
        return result;
    }
    result.domainId = domain->id;

    GraphContext ctx{graph, *domain, *sink, *sinkDef};
    s_buildContext(ctx);

    s_validate(ctx, result.diagnostics);
    if (result.hasErrors()) return result;

    // The full order from the sink drives dead-node reporting and the slice layout shared by passes
    std::vector<uint32_t> fullOrder;
    if (!s_topoSort(ctx, {graph.outputNodeId}, fullOrder)) {
        s_addDiagnostic(result.diagnostics, MaterialCompilerDiagnosticLevel::ERROR, "graph contains a cycle", UINT32_MAX);
        return result;
    }
    s_reportDeadNodes(ctx, fullOrder, result.diagnostics);
    s_assignResources(ctx, fullOrder, result.defaults, result.mapping, result.textureRefs);

    // graphId keeps the function name unique even when two graphs share a sanitized name
    std::string name = s_sanitizeName(graph.name) + "_" + std::to_string(graphId);

    for (size_t passIndex = 0; passIndex < domain->passes.size(); ++passIndex) {
        result.functions.push_back(s_emitPass(ctx, domain->passes[passIndex], passIndex, name, result.mapping));
    }

    result.success = true;
    return result;
}

} // namespace Rapture

#ifndef RAPTURE__MATERIAL_GRAPH_COMPILER_H
#define RAPTURE__MATERIAL_GRAPH_COMPILER_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "GraphDomain.h"
#include "MaterialGraph.h"
#include "materials/GraphInstanceData.h"

namespace Rapture {

/**
 * @brief Codegen scheme version, bumped when the emitted GLSL shape changes
 *
 * Stamped into the generated file so stale or incompatible output can be detected.
 */
constexpr uint32_t MATERIAL_GRAPH_COMPILER_VERSION = 3;

/**
 * @brief Severity of a compile diagnostic, ordered least to most severe
 */
enum class MaterialCompilerDiagnosticLevel {
    NONE,    // no severity, an unset default
    INFO,    // informational, e.g. a dead node was eliminated
    WARNING, // suspicious but compilable
    ERROR,   // compilation cannot proceed
};

/**
 * @brief One compile message, optionally tied to the node that caused it
 */
struct MaterialCompilerDiagnostic {
    MaterialCompilerDiagnosticLevel level = MaterialCompilerDiagnosticLevel::NONE;
    std::string message;
    uint32_t nodeId = UINT32_MAX; // UINT32_MAX means graph level, not a specific node
};

using GraphNodeId = uint32_t;
using GraphBufferOffset = uint32_t;
using GraphPinKey = uint64_t;

/**
 * @brief Pack a node id and its input pin index into a single slot mapping key
 * @param nodeId The owning node
 * @param pinIndex The input pin index on that node
 * @return The combined key
 */
inline GraphPinKey Graph_pinKey(GraphNodeId nodeId, uint32_t pinIndex)
{
    return (static_cast<uint64_t>(nodeId) << 32) | pinIndex;
}

/**
 * @brief An authored pin value packed into the instance slice: where it sits and its pin type
 */
struct GraphPoolSlot {
    GraphBufferOffset offset = 0;
    PinType type = PinType::FLOAT;
};

/**
 * @brief Where each authored input pin's value lives in the instance slice, for editor writes
 */
struct GraphSlotMapping {
    std::unordered_map<GraphPinKey, GraphPoolSlot> slots;
};

/**
 * @brief One compiled pass of a graph: which pass it fills and its GLSL function
 */
struct CompiledFunction {
    size_t passIndex = 0; // index into the graph's domain passes
    std::string functionName;
    std::string glslFunction;
};

/**
 * @brief Result of compiling one graph: the emitted GLSL passes plus the data they expect
 */
struct CompileResult {
    bool success = false;
    std::vector<MaterialCompilerDiagnostic> diagnostics;

    const GraphDomain *domain = nullptr;     // the domain the graph belongs to, resolved from its sink
    std::vector<CompiledFunction> functions; // one per domain pass, sharing the slice layout below
    uint32_t graphId = 0; // global identifier for the graph among all registered graphs

    GraphInstanceData defaults; // packed default slice, one uint per texture index and per value component
    GraphSlotMapping mapping;
    std::vector<AssetPtr<Texture>> textureRefs; // holds every texture the slice references so it is not evicted

    /**
     * @brief Whether any diagnostic is an error
     * @return True if at least one ERROR diagnostic was recorded
     */
    bool hasErrors() const
    {
        for (const auto &diagnostic : diagnostics) {
            if (diagnostic.level == MaterialCompilerDiagnosticLevel::ERROR) return true;
        }
        return false;
    }
};

/**
 * @brief Compiles a MaterialGraph into a straight-line GLSL surface function
 */
class MaterialGraphCompiler {
  public:
    /**
     * @brief Compile a graph to GLSL and its default instance pool
     * @param graph The authored graph to compile
     * @param graphId The id the manager assigned, baked into the function name and dispatcher case
     * @return The compile result; check success, and diagnostics for messages
     */
    CompileResult compile(const MaterialGraph &graph, uint32_t graphId);
};

} // namespace Rapture

#endif // RAPTURE__MATERIAL_GRAPH_COMPILER_H

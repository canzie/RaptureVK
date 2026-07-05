#ifndef RAPTURE__MATERIAL_GRAPH_COMPILER_H
#define RAPTURE__MATERIAL_GRAPH_COMPILER_H

#include <cstdint>
#include <string>
#include <unordered_map>

#include "MaterialGraph.h"
#include "materials/GraphInstanceData.h"

namespace Rapture {

/**
 * @brief Codegen scheme version, bumped when the emitted GLSL shape changes
 *
 * Stamped into the generated file so stale or incompatible output can be detected.
 */
constexpr uint32_t MATERIAL_GRAPH_COMPILER_VERSION = 1;

/**
 * @brief Where each resource node's value lives in the instance pool, for editor writes
 */
struct GraphSlotMapping {
    std::unordered_map<uint32_t, uint32_t> constantSlots; // maps a node id to its constants slot
    std::unordered_map<uint32_t, uint32_t> textureSlots;  // maps a node id to its textures slot
};

/**
 * @brief Result of compiling one graph: the emitted GLSL plus the data it expects
 */
struct CompileResult {
    bool success = false;
    std::string error;

    std::string functionName;
    std::string glslFunction;
    uint32_t dispatcherCase = 0; // graphId assigned by the manager on register

    GraphInstanceData defaults = GraphInstanceData::createDefault();
    GraphSlotMapping mapping;
};

/**
 * @brief Compiles a MaterialGraph into a straight-line GLSL surface function
 */
class MaterialGraphCompiler {
  public:
    /**
     * @brief Compile a graph to GLSL and its default instance pool
     * @param graph The authored graph to compile
     * @return The compile result; check success, and error on failure
     */
    CompileResult compile(const MaterialGraph &graph);
};

} // namespace Rapture

#endif // RAPTURE__MATERIAL_GRAPH_COMPILER_H

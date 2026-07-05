#ifndef RAPTURE__SURFACE_GRAPH_MANAGER_H
#define RAPTURE__SURFACE_GRAPH_MANAGER_H

#include <cstdint>
#include <filesystem>
#include <vector>

#include "MaterialGraph.h"
#include "MaterialGraphCompiler.h"

namespace Rapture {

/**
 * @brief Owns compiled surface graphs and emits the generated GLSL dispatcher file
 *
 * Each registered graph gets a graphId that doubles as its case in evalSurfaceGraph.
 */
class SurfaceGraphManager {
  public:
    SurfaceGraphManager();

    /**
     * @brief Compile a graph and assign it a graphId
     * @param graph The authored graph to compile
     * @return The assigned graphId, or UINT32_MAX if compilation failed
     */
    uint32_t registerGraph(const MaterialGraph &graph);

    /**
     * @brief Write every registered graph and the dispatcher to the generated GLSL file
     * @param path Destination path for generated/SurfaceGraphs.glsl
     * @return True if the file was written
     */
    bool writeGeneratedFile(const std::filesystem::path &path);

    /**
     * @brief Default instance pool a graph expects, for seeding a MaterialInstance
     * @param graphId The graph to query
     * @return The compiled defaults, or a zeroed pool if the id is unknown
     */
    GraphInstanceData getDefaults(uint32_t graphId) const;

    /**
     * @brief Slot mapping of a graph, for editor writes into the instance pool
     * @param graphId The graph to query
     * @return The slot mapping, or an empty mapping if the id is unknown
     */
    GraphSlotMapping getMapping(uint32_t graphId) const;

  private:
    std::vector<CompileResult> m_graphs;
    MaterialGraphCompiler m_compiler;
};

} // namespace Rapture

#endif // RAPTURE__SURFACE_GRAPH_MANAGER_H

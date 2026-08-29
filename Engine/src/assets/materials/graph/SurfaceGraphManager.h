#ifndef RAPTURE__SURFACE_GRAPH_MANAGER_H
#define RAPTURE__SURFACE_GRAPH_MANAGER_H

#include "assets/asset_manager/Asset.h"
#include <cstdint>
#include <filesystem>
#include <vector>

#include "MaterialGraph.h"
#include "MaterialGraphCompiler.h"

namespace Rapture {

class ATexture;

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
     * @brief Recompile a graph in place under its existing graphId
     * @param graphId The graph to replace
     * @param graph The new authored graph
     * @return True if it recompiled successfully
     */
    bool updateGraph(uint32_t graphId, const MaterialGraph &graph);

    /**
     * @brief Write one generated GLSL file per pass of every registered domain
     * @param directory Destination directory for the generated pass files
     * @return True if every file was written
     */
    bool writeGeneratedFiles(const std::filesystem::path &directory);

    /**
     * @brief Fire a source-changed event for every generated pass file so dependent shaders reload
     */
    void notifyShadersOfRegeneration() const;

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

    /**
     * @brief The textures a graph references, to retain alongside a seeded instance
     * @param graphId The graph to query
     * @return The texture references, or empty if the id is unknown
     */
    std::vector<Ref<ATexture>> getTextureRefs(uint32_t graphId) const;

  private:
    std::vector<CompileResult> m_graphs;
    MaterialGraphCompiler m_compiler;
};

} // namespace Rapture

#endif // RAPTURE__SURFACE_GRAPH_MANAGER_H

#include "SurfaceGraphManager.h"

#include <fstream>

#include "NodeRegistry.h"
#include "logging/Log.h"

namespace Rapture {

SurfaceGraphManager::SurfaceGraphManager()
{
    NodeRegistry::registerBuiltins();
}

uint32_t SurfaceGraphManager::registerGraph(const MaterialGraph &graph)
{
    CompileResult result = m_compiler.compile(graph);
    if (!result.success) {
        RP_CORE_ERROR("Failed to compile surface graph '{}': {}", graph.name, result.error);
        return UINT32_MAX;
    }

    uint32_t graphId = static_cast<uint32_t>(m_graphs.size());
    result.dispatcherCase = graphId;
    m_graphs.push_back(std::move(result));
    return graphId;
}

bool SurfaceGraphManager::writeGeneratedFile(const std::filesystem::path &path)
{
    std::string out;
    out += "/**\n";
    out += " * @file SurfaceGraphs.glsl\n";
    out += " * @brief Generated surface graph functions and their dispatcher\n";
    out += " * @author Rapture Material Graph Compiler\n";
    out += " * @version " + std::to_string(MATERIAL_GRAPH_COMPILER_VERSION) + "\n";
    out += " * @note DO NOT EDIT, this file is regenerated from material graphs\n";
    out += " */\n\n";
    out += "#ifndef SURFACE_GRAPHS_GLSL\n";
    out += "#define SURFACE_GRAPHS_GLSL\n\n";

    for (const auto &graph : m_graphs) {
        out += graph.glslFunction;
        out += "\n";
    }

    out += "SurfaceData evalSurfaceGraph(uint graphId, SurfaceInputs si, uint gii) {\n";
    out += "    switch (graphId) {\n";
    for (const auto &graph : m_graphs) {
        out += "        case " + std::to_string(graph.dispatcherCase) + "u: return " + graph.functionName + "(si, gii);\n";
    }
    out += "    }\n\n";
    out += "    SurfaceData surf;\n";
    out += "    surf.albedo = vec3(1.0, 0.0, 1.0);\n";
    out += "    surf.normal = normalize(si.worldNormal);\n";
    out += "    surf.roughness = 0.5;\n";
    out += "    surf.metallic = 0.0;\n";
    out += "    surf.ao = 1.0;\n";
    out += "    surf.shadingModelId = SM_OPENPBR_STANDARD;\n";
    out += "    return surf;\n";
    out += "}\n\n";
    out += "#endif // SURFACE_GRAPHS_GLSL\n";

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        RP_CORE_ERROR("Failed to open generated surface graph file for writing: {}", path.string());
        return false;
    }
    file << out;
    return true;
}

GraphInstanceData SurfaceGraphManager::getDefaults(uint32_t graphId) const
{
    if (graphId >= m_graphs.size()) return GraphInstanceData::createDefault();
    return m_graphs[graphId].defaults;
}

GraphSlotMapping SurfaceGraphManager::getMapping(uint32_t graphId) const
{
    if (graphId >= m_graphs.size()) return {};
    return m_graphs[graphId].mapping;
}

} // namespace Rapture

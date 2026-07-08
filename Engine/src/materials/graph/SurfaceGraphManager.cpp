#include "SurfaceGraphManager.h"

#include <fstream>
#include <string_view>

#include "NodeRegistry.h"
#include "logging/Log.h"

namespace Rapture {

static void s_logDiagnostics(std::string_view graphName, const CompileResult &result)
{
    for (const auto &diagnostic : result.diagnostics) {
        std::string where = diagnostic.nodeId == UINT32_MAX ? "" : (" (node " + std::to_string(diagnostic.nodeId) + ")");
        switch (diagnostic.level) {
        case MaterialCompilerDiagnosticLevel::ERROR:
            RP_CORE_ERROR("Surface graph '{}': {}{}", graphName, diagnostic.message, where);
            break;
        case MaterialCompilerDiagnosticLevel::WARNING:
            RP_CORE_WARN("Surface graph '{}': {}{}", graphName, diagnostic.message, where);
            break;
        case MaterialCompilerDiagnosticLevel::INFO:
        case MaterialCompilerDiagnosticLevel::NONE:
            RP_CORE_INFO("Surface graph '{}': {}{}", graphName, diagnostic.message, where);
            break;
        }
    }
}

SurfaceGraphManager::SurfaceGraphManager()
{
    NodeRegistry::registerBuiltins();
}

uint32_t SurfaceGraphManager::registerGraph(const MaterialGraph &graph)
{
    uint32_t graphId = static_cast<uint32_t>(m_graphs.size());
    CompileResult result = m_compiler.compile(graph, graphId);
    s_logDiagnostics(graph.name, result);

    if (!result.success) return UINT32_MAX;

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

    out += "SurfaceData evalSurfaceGraph(uint graphId, SurfaceInputs si, uint base) {\n";
    out += "    switch (graphId) {\n";
    for (const auto &graph : m_graphs) {
        out += "        case " + std::to_string(graph.graphId) + "u: return " + graph.functionName + "(si, base);\n";
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
    if (graphId >= m_graphs.size()) return {};
    return m_graphs[graphId].defaults;
}

GraphSlotMapping SurfaceGraphManager::getMapping(uint32_t graphId) const
{
    if (graphId >= m_graphs.size()) return {};
    return m_graphs[graphId].mapping;
}

std::vector<AssetPtr<Texture>> SurfaceGraphManager::getTextureRefs(uint32_t graphId) const
{
    if (graphId >= m_graphs.size()) {
        return {};
    }
    return m_graphs[graphId].textureRefs;
}

} // namespace Rapture

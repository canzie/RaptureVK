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

static const CompiledFunction *s_findVariant(const CompileResult &graph, SurfaceVariant variant)
{
    for (const auto &function : graph.functions) {
        if (function.variant == variant) return &function;
    }
    return nullptr;
}

static bool s_writeFile(const std::filesystem::path &path, const std::string &content)
{
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        RP_CORE_ERROR("Failed to open generated surface graph file for writing: {}", path.string());
        return false;
    }
    file << content;
    return true;
}

/**
 * @brief Build the generated file for one surface variant: its functions and dispatcher
 * @param graphs The registered graphs
 * @param variant Which compiled function of each graph to emit
 * @param structName The GLSL surface struct the variant returns
 * @param dispatcherName The dispatcher function name
 * @param guard The include guard macro
 * @param fallbackBody The dispatcher body for an unknown graph id
 * @return The file contents
 */
static std::string s_emitVariantFile(const std::vector<CompileResult> &graphs, SurfaceVariant variant, const char *structName,
                                     const char *dispatcherName, const char *guard, const char *fallbackBody)
{
    std::string out;
    out += "/**\n";
    out += " * @brief Generated surface graph functions and their dispatcher\n";
    out += " * @author Rapture Material Graph Compiler\n";
    out += " * @version " + std::to_string(MATERIAL_GRAPH_COMPILER_VERSION) + "\n";
    out += " * @note DO NOT EDIT, this file is regenerated from material graphs\n";
    out += " */\n\n";
    out += "#ifndef " + std::string(guard) + "\n";
    out += "#define " + std::string(guard) + "\n\n";

    for (const auto &graph : graphs) {
        if (const CompiledFunction *function = s_findVariant(graph, variant)) {
            out += function->glslFunction;
            out += "\n";
        }
    }

    out += std::string(structName) + " " + dispatcherName + "(uint graphId, SurfaceInputs si, uint base) {\n";
    out += "    switch (graphId) {\n";
    for (const auto &graph : graphs) {
        if (const CompiledFunction *function = s_findVariant(graph, variant)) {
            out += "        case " + std::to_string(graph.graphId) + "u: return " + function->functionName + "(si, base);\n";
        }
    }
    out += "    }\n\n";
    out += "    " + std::string(structName) + " surf;\n";
    out += fallbackBody;
    out += "    return surf;\n";
    out += "}\n\n";
    out += "#endif // " + std::string(guard) + "\n";
    return out;
}

bool SurfaceGraphManager::writeGeneratedFile(const std::filesystem::path &path)
{
    const char *gbufferFallback = "    surf.albedo = vec3(1.0, 0.0, 1.0);\n"
                                  "    surf.normal = normalize(si.worldNormal);\n"
                                  "    surf.roughness = 0.5;\n"
                                  "    surf.metallic = 0.0;\n"
                                  "    surf.ao = 1.0;\n"
                                  "    surf.emission = vec4(0.0);\n"
                                  "    surf.emissiveStrength = 0.0;\n"
                                  "    surf.shadingModelId = SM_OPENPBR_STANDARD;\n";
    const char *diffuseFallback = "    surf.albedo = vec3(1.0, 0.0, 1.0);\n"
                                  "    surf.normal = normalize(si.worldNormal);\n"
                                  "    surf.emission = vec4(0.0);\n"
                                  "    surf.emissiveStrength = 0.0;\n";

    std::string gbuffer = s_emitVariantFile(m_graphs, SurfaceVariant::GBUFFER, "SurfaceData", "evalSurfaceGraph",
                                            "SURFACE_GRAPHS_GLSL", gbufferFallback);
    std::string diffuse = s_emitVariantFile(m_graphs, SurfaceVariant::DIFFUSE, "SurfaceDataDiffuse", "evalSurfaceGraphDiffuse",
                                            "SURFACE_GRAPHS_DIFFUSE_GLSL", diffuseFallback);

    std::filesystem::path diffusePath = path.parent_path() / "SurfaceGraphsDiffuse.glsl";
    return s_writeFile(path, gbuffer) && s_writeFile(diffusePath, diffuse);
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

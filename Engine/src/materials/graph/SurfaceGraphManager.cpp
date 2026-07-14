#include "SurfaceGraphManager.h"

#include <fstream>
#include <string>
#include <string_view>
#include <unordered_set>

#include "GraphDomain.h"
#include "MaterialGraphTypes.h"
#include "events/ShaderEvents.h"
#include "logging/Log.h"
#include "window_context/Application.h"

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
    GraphDomainRegistry::registerBuiltins();
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

bool SurfaceGraphManager::updateGraph(uint32_t graphId, const MaterialGraph &graph)
{
    if (graphId >= m_graphs.size()) {
        RP_CORE_ERROR("Cannot update unknown graph id {}", graphId);
        return false;
    }

    CompileResult result = m_compiler.compile(graph, graphId);
    s_logDiagnostics(graph.name, result);

    if (!result.success) {
        return false;
    }

    m_graphs[graphId] = std::move(result);
    return true;
}

// The struct field type: a pin type, or an explicit override for a non-pin field like the model id
static std::string_view s_fieldGlslType(const GraphOutputField &field)
{
    return field.glslTypeOverride.empty() ? std::string_view(graph_pinTypeGlsl(field.type)) : field.glslTypeOverride;
}

// The dispatcher fallback value for a field: its debug errorFallback if set, else its normal fallback
static std::string_view s_fieldFallback(const GraphOutputField &field)
{
    return field.errorFallback.empty() ? field.fallback : field.errorFallback;
}

/**
 * @brief Build the generated file for one pass: its struct, every graph's function, and the dispatcher
 * @param graphs The registered graphs
 * @param domain The domain owning this pass
 * @param pass The pass to emit
 * @param passIndex The pass index, selecting each graph's function
 * @return The file contents
 */
static std::string s_emitPassFile(const std::vector<CompileResult> &graphs, const GraphDomain &domain, const GraphPass &pass,
                                  size_t passIndex)
{
    std::string out;
    out += "/**\n";
    out += " * @brief Generated surface graph functions and their dispatcher\n";
    out += " * @author Rapture Material Graph Compiler\n";
    out += " * @version " + std::to_string(MATERIAL_GRAPH_COMPILER_VERSION) + "\n";
    out += " * @note DO NOT EDIT, this file is regenerated from material graphs\n";
    out += " */\n\n";
    out += "#ifndef " + std::string(pass.guard) + "\n";
    out += "#define " + std::string(pass.guard) + "\n\n";

    // The pass output struct, the single source of truth the generated functions and consumers share
    out += "struct " + std::string(pass.structName) + " {\n";
    for (const auto &field : pass.fields) {
        out += "    " + std::string(s_fieldGlslType(field)) + " " + std::string(field.name) + ";\n";
    }
    out += "};\n\n";

    for (const auto &graph : graphs) {
        if (graph.domain != nullptr && graph.domain->sinkType == domain.sinkType && passIndex < graph.functions.size()) {
            out += graph.functions[passIndex].glslFunction;
            out += "\n";
        }
    }

    out += std::string(pass.structName) + " " + std::string(pass.dispatcherName) + "(uint graphId, " +
           std::string(domain.inputStructName) + " si, uint base) {\n";
    out += "    switch (graphId) {\n";
    for (const auto &graph : graphs) {
        if (graph.domain != nullptr && graph.domain->sinkType == domain.sinkType && passIndex < graph.functions.size()) {
            out += "        case " + std::to_string(graph.graphId) + "u: return " + graph.functions[passIndex].functionName +
                   "(si, base);\n";
        }
    }
    out += "    }\n\n";
    out += "    " + std::string(pass.structName) + " surf;\n";
    for (const auto &field : pass.fields) {
        out += "    surf." + std::string(field.name) + " = " + std::string(s_fieldFallback(field)) + ";\n";
    }
    out += "    return surf;\n";
    out += "}\n\n";
    out += "#endif // " + std::string(pass.guard) + "\n";
    return out;
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

bool SurfaceGraphManager::writeGeneratedFiles(const std::filesystem::path &directory)
{
    bool ok = true;
    for (const auto &domain : GraphDomainRegistry::all()) {
        for (size_t passIndex = 0; passIndex < domain.passes.size(); ++passIndex) {
            const GraphPass &pass = domain.passes[passIndex];
            std::string content = s_emitPassFile(m_graphs, domain, pass, passIndex);
            ok = s_writeFile(directory / std::string(pass.fileName), content) && ok;
        }
    }
    return ok;
}

void SurfaceGraphManager::notifyShadersOfRegeneration() const
{
    Application::getInstance().getVulkanContext().waitIdle();

    std::unordered_set<std::string_view> fired;
    for (const auto &domain : GraphDomainRegistry::all()) {
        for (const GraphPass &pass : domain.passes) {
            if (fired.insert(pass.fileName).second) {
                ShaderEvents::onShaderSourceChanged().publish(pass.fileName);
            }
        }
    }
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

#include "ShaderCompilation.h"
#include "Logging/Log.h"
#include <fstream>
#include <sstream>

namespace Rapture {
// --- ShaderIncluder Implementation ---
ShaderIncluder::ShaderIncluder(const std::filesystem::path &includePath) : m_includePath(includePath) {}

shaderc_include_result *ShaderIncluder::GetInclude(const char *requested_source, shaderc_include_type type,
                                                   const char *requesting_source, size_t include_depth)
{
    (void)type;
    (void)requesting_source;
    (void)include_depth;

    const std::filesystem::path requestedPath(requested_source);
    const std::filesystem::path fullPath = m_includePath / requestedPath;

    std::ifstream file(fullPath);
    if (!file.is_open()) {
        RP_CORE_ERROR("Could not open include file: {0}", fullPath.string());
        // Return an error to shaderc
        auto *error_data = new shaderc_include_result;
        error_data->source_name = "";
        error_data->source_name_length = 0;
        auto *error_message = new std::string("Could not open include file: " + fullPath.string());
        error_data->content = error_message->c_str();
        error_data->content_length = error_message->length();
        error_data->user_data = error_message; // To be deleted in ReleaseInclude
        return error_data;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    auto *data = new IncludeData{fullPath.string(), buffer.str()};

    auto *result = new shaderc_include_result;
    result->user_data = data;
    result->source_name = data->fullPath.c_str();
    result->source_name_length = data->fullPath.length();
    result->content = data->content.c_str();
    result->content_length = data->content.length();

    return result;
}

void ShaderIncluder::ReleaseInclude(shaderc_include_result *data)
{
    if (data->source_name_length == 0) { // This is an error result
        delete static_cast<std::string *>(data->user_data);
    } else {
        delete static_cast<IncludeData *>(data->user_data);
    }
    delete data;
}

// --- ShaderCompiler Implementation ---
ShaderCompiler::ShaderCompiler()
{
    if (!m_compiler.IsValid()) {
        RP_CORE_ERROR("Shaderc compiler is not valid.");
        throw std::runtime_error("Failed to initialize shader compiler");
    }
}

ShaderCompiler::~ShaderCompiler() {}

std::vector<char> ShaderCompiler::Compile(const std::filesystem::path &path, const ShaderCompileInfo &compileInfo)
{
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
#ifdef NDEBUG
    // shaderc_optimization_level_performance breaks ddgi
    options.SetOptimizationLevel(shaderc_optimization_level_zero);
#else
    options.SetOptimizationLevel(shaderc_optimization_level_zero);
#endif

    shaderc_shader_kind kind = getShaderKind(path);
    if (kind == (shaderc_shader_kind)-1) {
        RP_CORE_ERROR("Unknown shader file extension in path: {0}", path.string());
        return {};
    }

    std::string source = readFile(path);
    if (source.empty()) {
        RP_CORE_ERROR("Failed to read shader file: {0}", path.string());
        return {};
    }

    // Set include handler
    options.SetIncluder(std::make_unique<ShaderIncluder>(compileInfo.includePath));

    for (const auto &macro : compileInfo.macros) {
        options.AddMacroDefinition(macro);
    }

    shaderc::SpvCompilationResult module = m_compiler.CompileGlslToSpv(source, kind, path.string().c_str(), options);

    if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
        RP_CORE_ERROR("Failed to compile {0}:\n{1}", path.string(), module.GetErrorMessage());
        return {};
    }

    const uint32_t *spirv_data = module.cbegin();
    size_t spirv_size = std::distance(module.cbegin(), module.cend()) * sizeof(uint32_t);

    std::vector<char> spirv(spirv_size);
    memcpy(spirv.data(), spirv_data, spirv_size);

    RP_CORE_INFO("Compiled shader: {0} \n\t using macros: [{1}]", path.string(), fmt::join(compileInfo.macros, ", "));

    return spirv;
}

shaderc_shader_kind ShaderCompiler::getShaderKind(const std::filesystem::path &path)
{
    std::string filepath = path.string();
    if (filepath.find(".vert") != std::string::npos || filepath.find(".vs") != std::string::npos) return shaderc_glsl_vertex_shader;
    if (filepath.find(".frag") != std::string::npos || filepath.find(".fs") != std::string::npos)
        return shaderc_glsl_fragment_shader;
    if (filepath.find(".comp") != std::string::npos || filepath.find(".cs") != std::string::npos)
        return shaderc_glsl_compute_shader;
    if (filepath.find(".geom") != std::string::npos || filepath.find(".gs") != std::string::npos)
        return shaderc_glsl_geometry_shader;
    // Add other shader types if needed
    return (shaderc_shader_kind)-1;
}

std::string ShaderCompiler::readFile(const std::filesystem::path &path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
} // namespace Rapture

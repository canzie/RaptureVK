#include "ShaderCompilation.h"
#include "core/utils/Log.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace Rapture {

class ShaderIncluder : public glslang::TShader::Includer {
  public:
    ShaderIncluder(const std::filesystem::path& includePath) : m_includePath(includePath) {}

    IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t depth) override
    {
        (void)includerName;
        (void)depth;
        return readInclude(headerName);
    }

    IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t depth) override
    {
        (void)includerName;
        (void)depth;
        return readInclude(headerName);
    }

    void releaseInclude(IncludeResult* result) override
    {
        if (result != nullptr) {
            m_liveIncludes.erase(result);
        }
    }

  private:
    std::filesystem::path m_includePath;

    struct IncludeStorage {
        std::string path;
        std::string content;
    };
    std::unordered_map<IncludeResult*, std::pair<std::unique_ptr<IncludeStorage>, std::unique_ptr<IncludeResult>>> m_liveIncludes;

    IncludeResult* readInclude(const char* headerName)
    {
        const std::filesystem::path fullPath = m_includePath / headerName;
        std::ifstream file(fullPath);
        if (!file.is_open()) {
            RP_CORE_ERROR("Could not open include file: {}", fullPath.string());
            return nullptr;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();

        auto storage = std::make_unique<IncludeStorage>();
        storage->path = fullPath.string();
        storage->content = buffer.str();
        auto result = std::make_unique<IncludeResult>(
            storage->path, storage->content.c_str(), storage->content.size(), nullptr);

        IncludeResult* raw = result.get();
        m_liveIncludes.emplace(raw, std::make_pair(std::move(storage), std::move(result)));
        return raw;
    }
};

static bool s_initialized = false;

ShaderCompiler::ShaderCompiler()
{
    if (!s_initialized) {
        glslang::InitializeProcess();
        s_initialized = true;
    }
}

ShaderCompiler::~ShaderCompiler() {}

std::vector<char> ShaderCompiler::Compile(const std::filesystem::path &path, const ShaderCompileInfo &compileInfo)
{
    const int stage = getShaderStage(path);
    if (stage == -1) {
        RP_CORE_ERROR("Unknown shader file extension in path: {}", path.string());
        return {};
    }

    std::string source = readFile(path);
    if (source.empty()) {
        RP_CORE_ERROR("Failed to read shader file: {}", path.string());
        return {};
    }

    std::string preamble = "#extension GL_GOOGLE_include_directive : require\n";
    for (const auto& macro : compileInfo.macros) {
        preamble += "#define " + macro.name;
        if (!macro.value.empty()) preamble += " " + macro.value;
        preamble += "\n";
    }

    const EShLanguage eshStage = static_cast<EShLanguage>(stage);
    glslang::TShader shader(eshStage);

    const char* src = source.c_str();
    const int srcLen = static_cast<int>(source.size());
    const std::string pathStr = path.string();
    const char* srcName = pathStr.c_str();
    shader.setStringsWithLengthsAndNames(&src, &srcLen, &srcName, 1);
    shader.setPreamble(preamble.c_str());

    shader.setEnvInput(glslang::EShSourceGlsl, eshStage, glslang::EShClientVulkan, 460);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

    ShaderIncluder includer(compileInfo.includePath);

    if (!shader.parse(GetDefaultResources(), 460, false, EShMsgDefault, includer)) {
        RP_CORE_ERROR("Failed to compile {0}:\n{1}", path.string(), shader.getInfoLog());
        return {};
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(EShMsgDefault)) {
        RP_CORE_ERROR("Failed to link {0}:\n{1}", path.string(), program.getInfoLog());
        return {};
    }

    std::vector<uint32_t> spirvWords;
    glslang::SpvOptions spvOptions{};
    glslang::GlslangToSpv(*program.getIntermediate(eshStage), spirvWords, &spvOptions);

    std::vector<std::string> macroStrings;
    macroStrings.reserve(compileInfo.macros.size());
    for (const auto& macro : compileInfo.macros) {
        macroStrings.push_back(macro.value.empty() ? macro.name : macro.name + "=" + macro.value);
    }
    RP_CORE_INFO("Compiled shader: {0} \n\t using macros: [{1}]", path.string(), fmt::join(macroStrings, ", "));

    std::vector<char> spirv(spirvWords.size() * sizeof(uint32_t));
    memcpy(spirv.data(), spirvWords.data(), spirv.size());
    return spirv;
}

int ShaderCompiler::getShaderStage(const std::filesystem::path &path)
{
    const std::string p = path.string();
    if (p.find(".vert") != std::string::npos || p.find(".vs") != std::string::npos) return EShLangVertex;
    if (p.find(".frag") != std::string::npos || p.find(".fs") != std::string::npos) return EShLangFragment;
    if (p.find(".comp") != std::string::npos || p.find(".cs") != std::string::npos) return EShLangCompute;
    if (p.find(".geom") != std::string::npos || p.find(".gs") != std::string::npos) return EShLangGeometry;
    if (p.find(".tesc") != std::string::npos) return EShLangTessControl;
    if (p.find(".tese") != std::string::npos) return EShLangTessEvaluation;
    if (p.find(".mesh") != std::string::npos) return EShLangMesh;
    if (p.find(".task") != std::string::npos) return EShLangTask;
    return -1;
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

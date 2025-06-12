#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <shaderc/shaderc.hpp>

#include "ShaderCommon.h"

namespace Rapture
{
    // Custom includer to handle #include directives
    class ShaderIncluder : public shaderc::CompileOptions::IncluderInterface {
    public:
        ShaderIncluder(const std::filesystem::path& includePath);

        // Handles shaderc_include_resolver_fn callbacks.
        shaderc_include_result* GetInclude(const char* requested_source,
                                           shaderc_include_type type,
                                           const char* requesting_source,
                                           size_t include_depth) override;

        // Handles shaderc_include_result_release_fn callbacks.
        void ReleaseInclude(shaderc_include_result* data) override;
    
    private:
        std::filesystem::path m_includePath;

        struct IncludeData {
            std::string fullPath;
            std::string content;
        };
    };

    class ShaderCompiler
    {
    public:
        ShaderCompiler();
        ~ShaderCompiler();

        std::vector<char> Compile(const std::filesystem::path& path, const ShaderCompileInfo& compileInfo);

    private:
        shaderc_shader_kind getShaderKind(const std::filesystem::path& path);
        std::string readFile(const std::filesystem::path& path);

        shaderc::Compiler m_compiler;
    };
}

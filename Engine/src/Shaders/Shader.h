#pragma once

#include <vector>
#include <string>
#include <map>
#include <filesystem>

#include "vulkan/vulkan.h"


namespace Rapture {

    enum class ShaderType {
        VERTEX,
        FRAGMENT,
        GEOMETRY,
        COMPUTE
    };

    /*
    struct ShaderProgram {
        std::filesystem::path filepath;
        std::vector<std::string> defines;
        std::string entryPoint = "main";
        ShaderType type;

    };
    */

    std::string shaderTypeToString(ShaderType type);




    class Shader {
    public:
        Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
        Shader(const std::filesystem::path& computePath);
        ~Shader();

        // constructors for different shaders types
        void createGraphicsShader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
        void createComputeShader(const std::filesystem::path& computePath);

        // helper functions
        void createShaderModule(const std::vector<char>& code, ShaderType type);
        std::vector<char> readFile(const std::filesystem::path& path);

        // getters
        const std::vector<VkPipelineShaderStageCreateInfo>& getStages() { return m_stages; };
        const VkShaderModule& getSource(ShaderType type);

    private:
        std::map<ShaderType, VkShaderModule> m_sources;
        std::vector<VkPipelineShaderStageCreateInfo> m_stages;


    };



}


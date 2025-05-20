#include "Shader.h"

#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include <fstream>

namespace Rapture {

Shader::Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath) {
    createGraphicsShader(vertexPath, fragmentPath);
}

Shader::Shader(const std::filesystem::path &computePath)
{
}

Shader::~Shader() {
    Application& app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    for (auto& source : m_sources) {
        vkDestroyShaderModule(device, source.second, nullptr);
    }

}

void Shader::createGraphicsShader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath)
{
    std::vector<char> vertexCode = readFile(vertexPath);
    std::vector<char> fragmentCode = readFile(fragmentPath);

    createShaderModule(vertexCode, ShaderType::VERTEX);
    createShaderModule(fragmentCode, ShaderType::FRAGMENT);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = m_sources[ShaderType::VERTEX];
    vertShaderStageInfo.pName = "main";
    vertShaderStageInfo.pSpecializationInfo = nullptr; // constants i think idk

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = m_sources[ShaderType::FRAGMENT];
    fragShaderStageInfo.pName = "main";
    fragShaderStageInfo.pSpecializationInfo = nullptr; // constants i think idk

    m_stages.push_back(vertShaderStageInfo);
    m_stages.push_back(fragShaderStageInfo);

}

void Shader::createComputeShader(const std::filesystem::path &computePath) {
    std::vector<char> computeCode = readFile(computePath);

    createShaderModule(computeCode, ShaderType::COMPUTE);

    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = m_sources[ShaderType::COMPUTE];
    computeShaderStageInfo.pName = "main";
    computeShaderStageInfo.pSpecializationInfo = nullptr; // constants i think idk

    m_stages.push_back(computeShaderStageInfo);
    
}

void Shader::createShaderModule(const std::vector<char>& code, ShaderType type) {

    if (m_sources.find(type) != m_sources.end()) {
        RP_CORE_WARN("Shader::createShaderModule - shader module of type {0} already exists! overwriting...", shaderTypeToString(type));
    }

    Application& app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        RP_CORE_ERROR("Shader::createShaderModule - failed to create shader module!");
        throw std::runtime_error("Shader::createShaderModule - failed to create shader module!");
    }

    m_sources[type] = shaderModule;
}

std::vector<char> Shader::readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        RP_CORE_ERROR("Shader::readFile - failed to open file! {0}", path.string());
        throw std::runtime_error("Shader::readFile - failed to open file!");
    }

    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();

    return buffer;

}

const VkShaderModule &Shader::getSource(ShaderType type)
{
    return m_sources[type];
}



std::string shaderTypeToString(ShaderType type) {
    switch (type) {
        case ShaderType::VERTEX: return "VERTEX";
        case ShaderType::FRAGMENT: return "FRAGMENT";
        case ShaderType::GEOMETRY: return "GEOMETRY";
        case ShaderType::COMPUTE: return "COMPUTE";
        default: return "UNKNOWN";
    }
}


}
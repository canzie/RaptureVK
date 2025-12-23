#ifndef RAPTURE__SHADER_H
#define RAPTURE__SHADER_H

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "Buffers/Buffers.h"
#include "ShaderCommon.h"
#include "ShaderCompilation.h"
#include "ShaderReflections.h"
#include <vulkan/vulkan.h>

// SPIRV-Reflect is now in vendor directory
#include <spirv_reflect.h>

namespace Rapture {

enum class ShaderStatus {
    UNINITIALIZED,
    COMPILING,
    LINKING,
    READY,
    FAILED
};

void printDescriptorSetInfo(const DescriptorSetInfo &setInfo);
void printDescriptorSetInfos(const std::vector<DescriptorSetInfo> &setInfos);

void printPushConstantLayout(const PushConstantInfo &pushConstantInfo);
void printPushConstantLayouts(const std::vector<PushConstantInfo> &pushConstantInfos);

std::string shaderTypeToString(ShaderType type);

class Shader {
  public:
    Shader(const std::filesystem::path &vertexPath, const std::filesystem::path &fragmentPath, ShaderCompileInfo compileInfo = {});
    Shader(const std::filesystem::path &computePath, ShaderCompileInfo compileInfo = {});
    ~Shader();

    // constructors for different shaders types
    void createGraphicsShader(const std::filesystem::path &vertexPath, const std::filesystem::path &fragmentPath);
    void createGraphicsShader(const std::filesystem::path &vertexPath);
    void createComputeShader(const std::filesystem::path &computePath);

    void createShaderModule(const std::vector<char> &code, ShaderType type);
    void createDescriptorSetLayout();

    // getters
    const std::vector<VkPipelineShaderStageCreateInfo> &getStages() { return m_stages; };
    const VkShaderModule &getSource(ShaderType type);

    const std::vector<VkDescriptorSetLayout> &getDescriptorSetLayouts()
    {
        return m_descriptorSetLayouts;
    }; // Get SPIR-V bytecode for material parameter extraction        const std::vector<char>& getVertexSpirv() const { return
       // m_vertexSpirv; }        const std::vector<char>& getFragmentSpirv() const { return m_fragmentSpirv; }                //
       // Get shader name for debugging        std::string getName() const { return m_name; }

    const std::vector<DescriptorInfo> &getMaterialSets() const { return m_materialSets; }

    const std::vector<VkPushConstantRange> &getPushConstantLayouts() const { return m_pushConstantLayouts; }

    const std::vector<DetailedPushConstantInfo> &getDetailedPushConstants() const { return m_detailedPushConstants; }
    bool isReady() const { return m_status == ShaderStatus::READY; }

  private:
    // Add new private methods
    std::vector<DescriptorSetInfo> collectDescriptorSetInfo(const std::vector<char> &vertexSpirv,
                                                            const std::vector<char> &fragmentSpirv);
    void createDescriptorSetLayoutFromInfo(const DescriptorSetInfo &setInfo);

    std::map<ShaderType, VkShaderModule> m_sources;
    std::vector<VkPipelineShaderStageCreateInfo> m_stages;
    std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
    std::vector<DescriptorSetInfo> m_descriptorSetInfos; // Store for later use

    std::vector<VkPushConstantRange> m_pushConstantLayouts;
    std::vector<DetailedPushConstantInfo> m_detailedPushConstants;

    std::vector<DescriptorInfo> m_materialSets;

    ShaderCompileInfo m_compileInfo;

    ShaderCompiler m_compiler;

    ShaderStatus m_status = ShaderStatus::UNINITIALIZED;
};

} // namespace Rapture

#endif // RAPTURE__SHADER_H
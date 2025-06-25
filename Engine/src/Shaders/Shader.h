#pragma once

#include <vector>
#include <string>
#include <map>
#include <filesystem>


#include <vulkan/vulkan.h>
#include "Buffers/Buffers.h"
#include "ShaderReflections.h"
#include "ShaderCompilation.h"
#include "ShaderCommon.h"

// SPIRV-Reflect is now in vendor directory
#include <spirv_reflect.h>

namespace Rapture {




    void printDescriptorSetInfo(const DescriptorSetInfo& setInfo);
    void printDescriptorSetInfos(const std::vector<DescriptorSetInfo>& setInfos);

    void printPushConstantLayout(const PushConstantInfo& pushConstantInfo);
    void printPushConstantLayouts(const std::vector<PushConstantInfo>& pushConstantInfos);


    std::string shaderTypeToString(ShaderType type);


    class Shader {
    public:
        Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath, ShaderCompileInfo compileInfo = {});
        Shader(const std::filesystem::path& computePath, ShaderCompileInfo compileInfo = {});
        ~Shader();

        // constructors for different shaders types
        void createGraphicsShader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
        void createGraphicsShader(const std::filesystem::path& vertexPath);

        void createComputeShader(const std::filesystem::path& computePath);

        // helper functions
        void createShaderModule(const std::vector<char>& code, ShaderType type);
        std::vector<char> readFile(const std::filesystem::path& path);
        void createDescriptorSetLayout();
        
        
        // getters
        const std::vector<VkPipelineShaderStageCreateInfo>& getStages() { return m_stages; };
        const VkShaderModule& getSource(ShaderType type);

        const std::vector<VkDescriptorSetLayout>& getDescriptorSetLayouts() { return m_descriptorSetLayouts; }; // Get SPIR-V bytecode for material parameter extraction        const std::vector<char>& getVertexSpirv() const { return m_vertexSpirv; }        const std::vector<char>& getFragmentSpirv() const { return m_fragmentSpirv; }                // Get shader name for debugging        std::string getName() const { return m_name; }

        const std::vector<DescriptorInfo>& getMaterialSets() const { return m_materialSets; }

        const std::vector<VkPushConstantRange>& getPushConstantLayouts() const { return m_pushConstantLayouts; }

    private:
        // Add new private methods
        std::vector<DescriptorSetInfo> collectDescriptorSetInfo(const std::vector<char>& vertexSpirv, const std::vector<char>& fragmentSpirv);
        void createDescriptorSetLayoutFromInfo(const DescriptorSetInfo& setInfo);

        std::map<ShaderType, VkShaderModule> m_sources;
        std::vector<VkPipelineShaderStageCreateInfo> m_stages;
        std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
        std::vector<DescriptorSetInfo> m_descriptorSetInfos;  // Store for later use

        std::vector<VkPushConstantRange> m_pushConstantLayouts;

        std::vector<DescriptorInfo> m_materialSets;

        ShaderCompileInfo m_compileInfo;

        ShaderCompiler m_compiler;
    };



}


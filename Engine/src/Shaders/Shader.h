#pragma once

#include <vector>
#include <string>
#include <map>
#include <filesystem>

#include "vulkan/vulkan.h"
#include "Buffers/Buffers.h"
#include "ShaderReflections.h"

// SPIRV-Reflect is now in vendor directory
#include <spirv_reflect.h>

namespace Rapture {

    enum class ShaderType {
        VERTEX,
        FRAGMENT,
        GEOMETRY,
        COMPUTE
    };

    // neetly organises descriptor sets based on their usage
    // any common resources are stored in the first set
    // any data related to the material (albedo, metallic, emmisive, ...) will be in a seperate set
    enum class DESCRIPTOR_SET_INDICES : uint8_t {
        COMMON_RESOURCES = 0, // updated once per frame, global resources
        MATERIAL = 1, // updated per material
        OBJECT_RESOURCES = 2, // updated per object
        EXTRA_RESOURCES = 3
    };

    // Add these new structures
    struct DescriptorBindingInfo {
        uint32_t binding;
        VkDescriptorType descriptorType;
        uint32_t descriptorCount;
        VkShaderStageFlags stageFlags;
        std::string name;  // For debugging/logging
    };

    struct DescriptorSetInfo {
        uint32_t setNumber;
        std::vector<DescriptorBindingInfo> bindings;
    };

    void printDescriptorSetInfo(const DescriptorSetInfo& setInfo);
    void printDescriptorSetInfos(const std::vector<DescriptorSetInfo>& setInfos);

    void printPushConstantLayout(const PushConstantInfo& pushConstantInfo);
    void printPushConstantLayouts(const std::vector<PushConstantInfo>& pushConstantInfos);


    std::string shaderTypeToString(ShaderType type);




    class Shader {
    public:
        Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
        Shader(const std::filesystem::path& computePath);
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

        const std::vector<VkDescriptorSetLayout>& getDescriptorSetLayouts() { return m_descriptorSetLayouts; };                // Get SPIR-V bytecode for material parameter extraction        const std::vector<char>& getVertexSpirv() const { return m_vertexSpirv; }        const std::vector<char>& getFragmentSpirv() const { return m_fragmentSpirv; }                // Get shader name for debugging        std::string getName() const { return m_name; }

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

    };



}


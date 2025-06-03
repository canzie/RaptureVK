#pragma once

#include <vector>
#include <spirv_reflect.h>
#include <vulkan/vulkan.h>

namespace Rapture {

    struct DescriptorParamInfo {
        std::string name;
        std::string type;
        uint32_t size;
        uint32_t offset;
    };

    struct DescriptorInfo {
        std::string name;
        uint32_t setNumber;
        uint32_t binding;
        std::vector<DescriptorParamInfo> params;
    };

    // Push constant information for VkPushConstantRange
    struct PushConstantInfo {
        uint32_t offset;
        uint32_t size;
        VkShaderStageFlags stageFlags;
        std::string name;  // For debugging/identification
    };

    // Utility function to convert PushConstantInfo to VkPushConstantRange
    inline VkPushConstantRange pushConstantInfoToRange(const PushConstantInfo& pcInfo) {
        VkPushConstantRange range{};
        range.stageFlags = pcInfo.stageFlags;
        range.offset = pcInfo.offset;
        range.size = pcInfo.size;
        return range;
    }

    // Utility function to convert a vector of PushConstantInfo to a vector of VkPushConstantRange
    inline std::vector<VkPushConstantRange> pushConstantInfoToRanges(const std::vector<PushConstantInfo>& pcInfos) {
        std::vector<VkPushConstantRange> ranges;
        ranges.reserve(pcInfos.size());
        for (const auto& pcInfo : pcInfos) {
            ranges.push_back(pushConstantInfoToRange(pcInfo));
        }
        return ranges;
    }

    // Reflect and log shader information
    static void reflectShaderInfo(const std::vector<char>& spirvCode);
    
    // Helper to determine if a descriptor type is a texture
    bool isTextureDescriptorType(SpvReflectDescriptorType descriptorType);
    
    // Convert SPIR-V descriptor type to string (for debugging)
    std::string descriptorTypeToString(SpvReflectDescriptorType descriptorType);

    // Get descriptor info from the material set
    std::vector<DescriptorInfo> extractMaterialSets(const std::vector<char>& spirvCode);

    // New helper function to get a string representation of a SPIR-V type description
    std::string getSpirvTypeDescriptionString(const SpvReflectTypeDescription* typeDescription);

    // get push constant info for populating the VkPushConstantRange(s)
    // Extract and merge push constant information from multiple shader stages
    // Each pair in shaderCodeWithStages should contain the SPIR-V bytecode and the primary stage it belongs to.
    // The function will reflect on each SPIR-V, extract its push constants, and merge them
    // into a single list, combining stage flags for ranges that span multiple shaders.
    std::vector<PushConstantInfo> getCombinedPushConstantRanges(
        const std::vector<std::pair<const std::vector<char>&, VkShaderStageFlags>>& shaderCodeWithStages
    );

}

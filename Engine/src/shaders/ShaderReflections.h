#ifndef RAPTURE__SHADER_REFLECTIONS_H
#define RAPTURE__SHADER_REFLECTIONS_H

#include <spirv_reflect.h>
#include <string>
#include <unordered_map>
#include <vector>
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
    std::string name; // For debugging/identification
};

/**
 * @brief Metadata for push constant members parsed from GLSL annotations.
 *
 * Annotations are parsed from comments following member declarations:
 *   float kr;    // @range(0.0, 0.1) @default(0.0025) @name("Rayleigh")
 *   vec4 color;  // @color @default(1.0, 0.5, 0.2, 1.0)
 *   vec4 data;   // @hidden
 */
struct PushConstantMemberMetadata {
    std::string displayName;         ///< @name("...") - Human-readable name for UI
    float minValue = 0.0f;           ///< @range(min, max) - Minimum value for sliders
    float maxValue = 1.0f;           ///< @range(min, max) - Maximum value for sliders
    std::vector<float> defaultValue; ///< @default(...) - Default value(s)
    bool hidden = false;             ///< @hidden - Don't show in UI
    bool isColor = false;            ///< @color - Use color picker for vec3/vec4
    bool hasRange = false;           ///< True if @range was specified
    bool hasDefault = false;         ///< True if @default was specified
};

struct PushConstantMemberInfo {
    std::string name;
    std::string type;
    uint32_t offset;
    uint32_t size;
    uint32_t arraySize;
    PushConstantMemberMetadata metadata; ///< Parsed annotations from GLSL comments

    enum class BaseType {
        FLOAT,
        INT,
        UINT,
        VEC2,
        VEC3,
        VEC4,
        MAT4,
        UNKNOWN
    };
    BaseType getBaseType() const;
};

struct DetailedPushConstantInfo {
    uint32_t offset;
    uint32_t size;
    VkShaderStageFlags stageFlags;
    std::string blockName;
    std::vector<PushConstantMemberInfo> members;
};

// Utility function to convert PushConstantInfo to VkPushConstantRange
inline VkPushConstantRange pushConstantInfoToRange(const PushConstantInfo &pcInfo)
{
    VkPushConstantRange range{};
    range.stageFlags = pcInfo.stageFlags;
    range.offset = pcInfo.offset;
    range.size = pcInfo.size;
    return range;
}

// Utility function to convert a vector of PushConstantInfo to a vector of VkPushConstantRange
inline std::vector<VkPushConstantRange> pushConstantInfoToRanges(const std::vector<PushConstantInfo> &pcInfos)
{
    std::vector<VkPushConstantRange> ranges;
    ranges.reserve(pcInfos.size());
    for (const auto &pcInfo : pcInfos) {
        ranges.push_back(pushConstantInfoToRange(pcInfo));
    }
    return ranges;
}

// Helper to determine if a descriptor type is a texture
bool isTextureDescriptorType(SpvReflectDescriptorType descriptorType);

// Convert SPIR-V descriptor type to string (for debugging)
std::string descriptorTypeToString(SpvReflectDescriptorType descriptorType);

// Get descriptor info from the material set
std::vector<DescriptorInfo> extractMaterialSets(const std::vector<char> &spirvCode);

// New helper function to get a string representation of a SPIR-V type description
std::string getSpirvTypeDescriptionString(const SpvReflectTypeDescription *typeDescription);

// get push constant info for populating the VkPushConstantRange(s)
// Extract and merge push constant information from multiple shader stages
// Each pair in shaderCodeWithStages should contain the SPIR-V bytecode and the primary stage it belongs to.
// The function will reflect on each SPIR-V, extract its push constants, and merge them
// into a single list, combining stage flags for ranges that span multiple shaders.
std::vector<PushConstantInfo>
getCombinedPushConstantRanges(const std::vector<std::pair<std::vector<char>, VkShaderStageFlags>> &shaderCodeWithStages);

// Extract detailed push constant information including member-level data
std::vector<DetailedPushConstantInfo> extractDetailedPushConstants(const std::vector<char> &spirvCode);

/**
 * @brief Parse push constant annotations from GLSL source code.
 *
 * Extracts metadata from comments in push_constant blocks. Supported annotations:
 *   @range(min, max)     - Value range for UI sliders
 *   @default(value, ...) - Default value(s)
 *   @name("Display Name") - Human-readable name
 *   @hidden              - Hide from UI
 *   @color               - Use color picker (for vec3/vec4)
 *
 * @param glslSource The raw GLSL source code
 * @return Map of member name to metadata
 */
std::unordered_map<std::string, PushConstantMemberMetadata> parsePushConstantAnnotations(const std::string &glslSource);

/**
 * @brief Apply parsed annotations to detailed push constant info.
 *
 * Matches annotations by member name and populates the metadata field.
 *
 * @param detailedInfo The reflected push constant info to update
 * @param annotations Parsed annotations from parsePushConstantAnnotations
 */
void applyPushConstantAnnotations(std::vector<DetailedPushConstantInfo> &detailedInfo,
                                  const std::unordered_map<std::string, PushConstantMemberMetadata> &annotations);

} // namespace Rapture

#endif // RAPTURE__SHADER_REFLECTIONS_H

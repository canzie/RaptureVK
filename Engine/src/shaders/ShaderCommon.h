#ifndef RAPTURE__SHADERCOMMON_H
#define RAPTURE__SHADERCOMMON_H

#include <vulkan/vulkan.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Rapture {

enum class ShaderType {
    VERTEX,
    FRAGMENT,
    GEOMETRY,
    COMPUTE,
    TESSELLATION_CONTROL,
    TESSELLATION_EVALUATION,
    MESH,
    TASK
};

// Convert ShaderType to Vulkan stage flag
inline VkShaderStageFlagBits shaderTypeToVkStage(ShaderType type)
{
    switch (type) {
    case ShaderType::VERTEX:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderType::FRAGMENT:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case ShaderType::GEOMETRY:
        return VK_SHADER_STAGE_GEOMETRY_BIT;
    case ShaderType::COMPUTE:
        return VK_SHADER_STAGE_COMPUTE_BIT;
    case ShaderType::TESSELLATION_CONTROL:
        return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    case ShaderType::TESSELLATION_EVALUATION:
        return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    case ShaderType::MESH:
        return VK_SHADER_STAGE_MESH_BIT_EXT;
    case ShaderType::TASK:
        return VK_SHADER_STAGE_TASK_BIT_EXT;
    default:
        return VK_SHADER_STAGE_ALL;
    }
}

// neetly organises descriptor sets based on their usage
// any common resources are stored in the first set
// any data related to the material (albedo, metallic, emmisive, ...) will be in a seperate set
enum class DESCRIPTOR_SET_INDICES : uint8_t {
    COMMON_RESOURCES = 0, // updated once per frame, global resources
    MATERIAL = 1,         // updated per material
    OBJECT_RESOURCES = 2, // updated per object
    EXTRA_RESOURCES = 3
};

// Add these new structures
struct DescriptorBindingInfo {
    uint32_t binding;
    VkDescriptorType descriptorType;
    uint32_t descriptorCount;
    VkShaderStageFlags stageFlags;
    std::string name; // For debugging/logging
};

struct DescriptorSetInfo {
    uint32_t setNumber;
    std::vector<DescriptorBindingInfo> bindings;
};

struct ShaderMacro {
    std::string name;
    std::string value; // Empty string means no value (just #define NAME)

    ShaderMacro(const std::string& n) : name(n), value("") {}
    ShaderMacro(const std::string& n, const std::string& v) : name(n), value(v) {}

    bool operator==(const ShaderMacro& other) const {
        return name == other.name && value == other.value;
    }
};

struct ShaderCompileInfo {
    std::filesystem::path includePath = "";
    std::vector<ShaderMacro> macros = {};
};

} // namespace Rapture

#endif // RAPTURE__SHADERCOMMON_H
#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>
#include <string>
#include <vector>

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
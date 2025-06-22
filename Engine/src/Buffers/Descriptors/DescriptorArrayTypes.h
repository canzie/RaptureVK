#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include "Buffers/Descriptors/DescriptorArrays/BufferDescriptorArray.h"


namespace Rapture {




enum class DescriptorArrayType {
    TEXTURE,
    STORAGE_BUFFER,
    UNIFORM_BUFFER,
    // Add more types as needed
};

struct DescriptorArrayConfig {
    DescriptorArrayType arrayType;
    uint32_t capacity = 0;
    std::string name = "";
    uint32_t bindingIndex = 0;
    
    // Convert to Vulkan descriptor type
    VkDescriptorType getTypeVK() const {
        switch (arrayType) {
            case DescriptorArrayType::TEXTURE:
                return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case DescriptorArrayType::STORAGE_BUFFER:
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case DescriptorArrayType::UNIFORM_BUFFER:
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            default:
                return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        }
    }
};

inline std::string getDescriptorArrayTypeName(DescriptorArrayType type) {
    switch (type) {
        case DescriptorArrayType::TEXTURE:
            return "Texture";
        case DescriptorArrayType::STORAGE_BUFFER:
            return "Storage Buffer";
        case DescriptorArrayType::UNIFORM_BUFFER:
            return "Uniform Buffer";
        default:
            return "Unknown";
    }
}

struct SubAllocationRequest {
    DescriptorArrayType type;
    uint32_t capacity;
    std::string name;
};

} // namespace Rapture 
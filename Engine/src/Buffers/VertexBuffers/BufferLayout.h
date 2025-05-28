#pragma once


#include "Buffers/Buffers.h"

#include <stdexcept>
#include <functional>

namespace Rapture {

// minimum 16 available binding indices
enum class BufferAttributeID {
    POSITION=0,
    NORMAL=1,
    TEXCOORD_0=2,
    TANGENT=3,
    BITANGENT=4,
    WEIGHTS_0=5,
    JOINTS_0=6,
    COLOR=7,
    TEXCOORD_1=8,
    WEIGHTS_1=9,
    JOINTS_1=10

};

BufferAttributeID stringToBufferAttributeID(const std::string& str);
std::string bufferAttributeIDToString(BufferAttributeID id);

struct BufferAttribute {
    BufferAttributeID name;
    uint32_t componentType; // int, float, etc
    std::string type; // vec2, vec3, etc
    uint32_t offset;

    uint32_t getSizeInBytes() const {
        uint32_t elementSize = 1; // For SCALAR
        if (type == "VEC2") elementSize = 2;
        else if (type == "VEC3") elementSize = 3;
        else if (type == "VEC4") elementSize = 4;
        else if (type == "MAT4") elementSize = 16;

        uint32_t componentSize = 1;
        switch (componentType) {
            case UNSIGNED_BYTE_TYPE: case BYTE_TYPE: componentSize = 1; break; // BYTE, UNSIGNED_BYTE
            case UNSIGNED_SHORT_TYPE: case SHORT_TYPE: componentSize = 2; break; // SHORT, UNSIGNED_SHORT
            case UNSIGNED_INT_TYPE: case INT_TYPE: case FLOAT_TYPE: componentSize = 4; break; // INT/UNSIGNED_INT, FLOAT
        }

        return elementSize * componentSize;
    }

    VkFormat getVkFormat() const {
        // Map component types and vector types to VkFormat
        if (componentType == FLOAT_TYPE) {
            if (type == "SCALAR") return VK_FORMAT_R32_SFLOAT;
            else if (type == "VEC2") return VK_FORMAT_R32G32_SFLOAT;
            else if (type == "VEC3") return VK_FORMAT_R32G32B32_SFLOAT;
            else if (type == "VEC4") return VK_FORMAT_R32G32B32A32_SFLOAT;
        }
        else if (componentType == INT_TYPE) {
            if (type == "SCALAR") return VK_FORMAT_R32_SINT;
            else if (type == "VEC2") return VK_FORMAT_R32G32_SINT;
            else if (type == "VEC3") return VK_FORMAT_R32G32B32_SINT;
            else if (type == "VEC4") return VK_FORMAT_R32G32B32A32_SINT;
        }
        else if (componentType == UNSIGNED_INT_TYPE) {
            if (type == "SCALAR") return VK_FORMAT_R32_UINT;
            else if (type == "VEC2") return VK_FORMAT_R32G32_UINT;
            else if (type == "VEC3") return VK_FORMAT_R32G32B32_UINT;
            else if (type == "VEC4") return VK_FORMAT_R32G32B32A32_UINT;
        }
        else if (componentType == SHORT_TYPE) {
            if (type == "SCALAR") return VK_FORMAT_R16_SINT;
            else if (type == "VEC2") return VK_FORMAT_R16G16_SINT;
            else if (type == "VEC3") return VK_FORMAT_R16G16B16_SINT;
            else if (type == "VEC4") return VK_FORMAT_R16G16B16A16_SINT;
        }
        else if (componentType == UNSIGNED_SHORT_TYPE) {
            if (type == "SCALAR") return VK_FORMAT_R16_UINT;
            else if (type == "VEC2") return VK_FORMAT_R16G16_UINT;
            else if (type == "VEC3") return VK_FORMAT_R16G16B16_UINT;
            else if (type == "VEC4") return VK_FORMAT_R16G16B16A16_UINT;
        }
        else if (componentType == BYTE_TYPE) {
            if (type == "SCALAR") return VK_FORMAT_R8_SINT;
            else if (type == "VEC2") return VK_FORMAT_R8G8_SINT;
            else if (type == "VEC3") return VK_FORMAT_R8G8B8_SINT;
            else if (type == "VEC4") return VK_FORMAT_R8G8B8A8_SINT;
        }
        else if (componentType == UNSIGNED_BYTE_TYPE) {
            if (type == "SCALAR") return VK_FORMAT_R8_UINT;
            else if (type == "VEC2") return VK_FORMAT_R8G8_UINT;
            else if (type == "VEC3") return VK_FORMAT_R8G8B8_UINT;
            else if (type == "VEC4") return VK_FORMAT_R8G8B8A8_UINT;
        }

        // Default fallback - should not happen with valid input
        return VK_FORMAT_UNDEFINED;
    }

    VkVertexInputAttributeDescription getVkDescription(uint32_t location, uint32_t binding) const {
        VkVertexInputAttributeDescription description{};
        description.binding = binding;
        description.location = location;
        description.format = getVkFormat();
        description.offset = offset;
        return description;
    }

    VkVertexInputAttributeDescription2EXT getVkDescription2EXT(uint32_t location, uint32_t binding) const {
        VkVertexInputAttributeDescription2EXT description{};
        description.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT;
        description.binding = binding;
        description.location = location;
        description.format = getVkFormat();
        description.offset = offset;
        return description;
    }
    bool operator==(const BufferAttribute& other) const {
        return (other.name == name &&
            other.offset == offset &&
            other.componentType == componentType &&
            other.type == type);
    }

    bool operator!=(const BufferAttribute& other) const {
        return !(*this == other);
    }
};

struct BufferLayout {
    std::vector<BufferAttribute> buffer_attribs;
	bool isInterleaved = false;     // Whether vertex data is interleaved (PNTPNT...) or not (PPP...NNN...TTT...)
	uint32_t vertexSize = 0;          // Total size of a vertex in bytes (used for interleaved format)
    uint32_t binding = 0;           // since we only use 1 buffer for all of the vertex data, binding should stay 1

    // Calculate the total vertex size for interleaved format
    uint32_t calculateVertexSize() {
        vertexSize = 0;
        for (const auto& attrib : buffer_attribs) {
            vertexSize += attrib.getSizeInBytes();
        }
        return vertexSize;
    }

    size_t hash() const {
    size_t hash = 0;
    for (const auto& attrib : buffer_attribs) {
        // Combine hashes of the attribute properties
        size_t attribHash = std::hash<std::string>()(bufferAttributeIDToString(attrib.name)) ^
                    (std::hash<unsigned int>()(attrib.componentType) << 1) ^
                    (std::hash<std::string>()(attrib.type) << 2) ^
                    (std::hash<size_t>()(attrib.offset) << 3);
        hash ^= attribHash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    // Add interleaved flag to the hash
    hash ^= std::hash<bool>()(isInterleaved) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    return hash;
}

    VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = binding;
        bindingDescription.stride = calculateVertexSize();
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
        for (const auto& attrib : buffer_attribs) {
            attributeDescriptions.push_back(attrib.getVkDescription(static_cast<uint32_t>(attrib.name), binding));
        }
        return attributeDescriptions;
    }

    VkVertexInputBindingDescription2EXT getBindingDescription2EXT() {
        VkVertexInputBindingDescription2EXT bindingDescription{};
        bindingDescription.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT;
        bindingDescription.binding = binding;
        bindingDescription.stride = calculateVertexSize();
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescription.divisor = 1;
        return bindingDescription;
    }

    std::vector<VkVertexInputAttributeDescription2EXT> getAttributeDescriptions2EXT() {
        std::vector<VkVertexInputAttributeDescription2EXT> attributeDescriptions;
        for (const auto& attrib : buffer_attribs) {
            attributeDescriptions.push_back(attrib.getVkDescription2EXT(static_cast<uint32_t>(attrib.name), binding));
        }
        return attributeDescriptions;
    }


};



}

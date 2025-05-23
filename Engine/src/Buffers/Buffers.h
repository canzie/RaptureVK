#pragma once

#include "glm/glm.hpp"

#include "vma/vk_mem_alloc.h"

#include <array>
#include <memory>
#include <vector>

namespace Rapture {

enum class BufferUsage {
    STATIC, // gpu only
    DYNAMIC, // host visible
    STREAM, // coherent
    STAGING // host visible and coherent
};


struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        return attributeDescriptions;
    }


};

    class Buffer {
    public:
        Buffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator);

        virtual ~Buffer();

        //TODO: temp way to destroy object, once the device has been abstracted, destroy in object destructor
        // for now we cant save the device as the VulkanContext destructor will delete the device before this object goes out of scope

        virtual void destoryObjects();

        virtual void addData(void* newData, VkDeviceSize size, VkDeviceSize offset);
        // needs to be subclass specific because of the staging buffer being created
        // could probably find a way around it but its fine
        virtual void addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset) = 0;

        VkBuffer& getBufferVk() { return m_Buffer; }
        VkDeviceSize getSize() const { return m_Size; }

        virtual VkBufferUsageFlags getBufferUsage() = 0;
        virtual VkMemoryPropertyFlags getMemoryPropertyFlags() = 0;

    protected:
        void createBuffer();
        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    protected:
        VkBuffer m_Buffer;
        VkBufferUsageFlags m_usageFlags;
        VkMemoryPropertyFlags m_propertiesFlags;
        VmaAllocation m_Allocation;
        VkDeviceSize m_Size;

        BufferUsage m_usage;

        VmaAllocator m_Allocator;


    };


}

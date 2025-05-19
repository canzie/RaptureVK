#pragma once

#include "glm/glm.hpp"
#include "Vulkan/vulkan.h"

#include <array>
#include <memory>
#include <vector>

namespace Rapture {


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
        Buffer(VkDeviceSize size, std::shared_ptr<VkDevice> device, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkPhysicalDevice physicalDevice);
        ~Buffer();

        //TODO: temp way to destroy object, once the device has been abstracted, destroy in object destructor
        // for now we cant save the device as the VulkanContext destructor ill delete the device before this object goes out of scope

        void destoryObjects();


        void addData(void* newData, VkDeviceSize size, VkDeviceSize offset);

        VkBuffer& getBuffer() { return m_Buffer; }
        VkDeviceMemory& getMemory() { return m_Memory; }




    private:
        void createBuffer(VkDeviceSize size, VkMemoryPropertyFlags properties, VkPhysicalDevice physicalDevice);

        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDevice physicalDevice);

    private:
        VkBuffer m_Buffer;
        VkDeviceMemory m_Memory;
        VkBufferUsageFlags m_Usage;
        VkMemoryPropertyFlags m_Properties;

        std::weak_ptr<VkDevice> m_Device;

    };


}

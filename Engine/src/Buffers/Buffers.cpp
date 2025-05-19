#include "Buffers.h"

#include "Logging/Log.h"
#include "stdexcept"

namespace Rapture {

    Buffer::Buffer(VkDeviceSize size, std::shared_ptr<VkDevice> device, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkPhysicalDevice physicalDevice) {
     

        m_Device = device;

        m_Usage = usage;
        m_Properties = properties;

        createBuffer(size, properties, physicalDevice);

    }


    Buffer::~Buffer() {
    }

    void Buffer::destoryObjects()
    {
        if (auto device = m_Device.lock()) {
            vkDestroyBuffer(*device, m_Buffer, nullptr);
            vkFreeMemory(*device, m_Memory, nullptr);
        }
    }

    void Buffer::addData(void* newData, VkDeviceSize size, VkDeviceSize offset)
    {
        if (!(m_Properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            RP_CORE_ERROR("Buffer::addData - Vertex buffer is not host visible!");
            return;
        }
    
        if (auto device = m_Device.lock()) {
            void* data;
            vkMapMemory(*device, m_Memory, 0, size, 0, &data);
            memcpy(data, newData, (size_t) size);
            vkUnmapMemory(*device, m_Memory);
        }
        
    }


    void Buffer::createBuffer( 
        VkDeviceSize size, 
        VkMemoryPropertyFlags properties,
        VkPhysicalDevice physicalDevice)
    { 
        auto device = m_Device.lock();
        
        if (!device) {
            RP_CORE_ERROR("failed to create vertex buffer!");
            return;
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = m_Usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(*device, &bufferInfo, nullptr, &m_Buffer) != VK_SUCCESS) {
            RP_CORE_ERROR("failed to create vertex buffer!");
            throw std::runtime_error("failed to create vertex buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(*device, m_Buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties, physicalDevice);

        if (vkAllocateMemory(*device, &allocInfo, nullptr, &m_Memory) != VK_SUCCESS) {
            RP_CORE_ERROR("failed to allocate vertex buffer memory!");
            throw std::runtime_error("failed to allocate vertex buffer memory!");
        }

        vkBindBufferMemory(*device, m_Buffer, m_Memory, 0);
        

    }

    uint32_t Buffer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDevice physicalDevice) {


        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("failed to find suitable memory type!");

    }
}
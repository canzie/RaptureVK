#pragma once

#include "glm/glm.hpp"

#include "vma/vk_mem_alloc.h"

#include "Utils/GLTypes.h"

#include <array>
#include <memory>
#include <vector>
#include <string>

namespace Rapture {

enum class BufferUsage {
    STATIC, // gpu only
    DYNAMIC, // host visible
    STREAM, // coherent
    STAGING // host visible and coherent
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

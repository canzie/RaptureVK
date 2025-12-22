#pragma once

#include <glm/glm.hpp>

#include <vk_mem_alloc.h>

#include "Utils/GLTypes.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace Rapture {

struct BufferAllocation;

enum class BufferUsage {
    STATIC,  // gpu only
    DYNAMIC, // host visible
    STREAM,  // coherent
    STAGING  // host visible and coherent
};

class Buffer : public std::enable_shared_from_this<Buffer> {
  public:
    Buffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator);

    virtual ~Buffer();

    virtual void destoryObjects();

    virtual void addData(void *newData, VkDeviceSize size, VkDeviceSize offset);
    // needs to be subclass specific because of the staging buffer being created
    // could probably find a way around it but its fine
    virtual void addDataGPU(void *data, VkDeviceSize size, VkDeviceSize offset) = 0;

    VkBuffer getBufferVk() const;
    VkDeviceSize getSize() const;
    VkDeviceSize getOffset() const;

    VkDescriptorBufferInfo getDescriptorBufferInfo() const;

    virtual VkBufferUsageFlags getBufferUsage() = 0;
    virtual VkMemoryPropertyFlags getMemoryPropertyFlags() = 0;

    std::shared_ptr<BufferAllocation> getBufferAllocation();

    // Get VMA allocation for mapping/unmapping
    VmaAllocation getAllocation() const { return m_Allocation; }

  protected:
    void setBufferAllocation(std::shared_ptr<BufferAllocation> allocation);
    void createBuffer();
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize dstOffset = 0);

  protected:
    // only valid when not using a bufferpool, prefer using the getBufferVk() to always get the correct buffer
    VkBuffer m_Buffer;

    VkBufferUsageFlags m_usageFlags;
    VkMemoryPropertyFlags m_propertiesFlags;
    VmaAllocation m_Allocation;
    VkDeviceSize m_Size;

    // only used for index and vertex buffers
    std::shared_ptr<BufferAllocation> m_bufferAllocation;

    BufferUsage m_usage;

    VmaAllocator m_Allocator;
};

} // namespace Rapture

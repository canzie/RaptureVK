#include "Buffers.h"

#include "Buffers/CommandBuffers/CommandPool.h"
#include "WindowContext/Application.h"

#include "Buffers/BufferPool.h"
#include "Logging/Log.h"
#include "stdexcept"

namespace Rapture {

Buffer::Buffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator)
    : m_Buffer(VK_NULL_HANDLE), m_Allocation(VK_NULL_HANDLE), m_Size(size), m_bufferAllocation(nullptr), m_usage(usage),
      m_Allocator(allocator)
{
}

Buffer::~Buffer()
{
    if (!m_bufferAllocation)
        destoryObjects();
}

void Buffer::destoryObjects()
{
    if (m_Buffer != VK_NULL_HANDLE && m_Allocation != VK_NULL_HANDLE)
        vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);
}

void Buffer::addData(void *newData, VkDeviceSize size, VkDeviceSize offset)
{
    // Check for buffer overflow
    if (offset + size > m_Size) {
        RP_CORE_ERROR("Buffer::addData - Buffer overflow detected! Attempted to write {} bytes at offset {} in buffer of size {}",
                      size, offset, m_Size);
        return;
    }

    if (!(m_propertiesFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
        RP_CORE_ERROR("Buffer::addData - Buffer is not host visible! Use addDataGPU for device local buffers.");
        return;
    }

    void *mappedData;
    if (vmaMapMemory(m_Allocator, m_Allocation, &mappedData) != VK_SUCCESS) {
        RP_CORE_ERROR("Buffer::addData - Failed to map memory!");
        return;
    }

    // Copy the data
    char *dst = (char *)mappedData + offset;
    memcpy(dst, newData, size);

    // If memory is not host coherent, we need to flush
    if (!(m_propertiesFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        vmaFlushAllocation(m_Allocator, m_Allocation, offset, size);
    }

    vmaUnmapMemory(m_Allocator, m_Allocation);
}

void Buffer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize dstOffset)
{
    auto &app = Application::getInstance();
    auto queueFamilyIndices = app.getVulkanContext().getQueueFamilyIndices();

    CommandPoolConfig config;
    config.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    config.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    config.threadId = 0;

    auto commandPool = CommandPoolManager::createCommandPool(config);

    // Create command buffer for transfer
    auto commandBuffer = commandPool->getCommandBuffer();

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer->getCommandBufferVk(), &beginInfo);
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer->getCommandBufferVk(), srcBuffer, dstBuffer, 1, &copyRegion);

    commandBuffer->end();
    // Submit command buffer

    auto &vulkanContext = app.getVulkanContext();

    auto queue = vulkanContext.getGraphicsQueue();
    // queue->addCommandBuffer(commandBuffer);
    queue->submitQueue(commandBuffer, VK_NULL_HANDLE);
    queue->waitIdle();
}

VkDescriptorBufferInfo Buffer::getDescriptorBufferInfo() const
{
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = getBufferVk();
    bufferInfo.offset = getOffset();
    bufferInfo.range = getSize();
    return bufferInfo;
}

VkBuffer Buffer::getBufferVk() const
{
    if (m_bufferAllocation)
        return m_bufferAllocation->getBuffer();
    return m_Buffer;
}

VkDeviceSize Buffer::getSize() const
{
    if (m_bufferAllocation)
        return m_bufferAllocation->sizeBytes;
    return m_Size;
}

VkDeviceSize Buffer::getOffset() const
{
    if (m_bufferAllocation)
        return m_bufferAllocation->offsetBytes;
    return 0;
}

std::shared_ptr<BufferAllocation> Buffer::getBufferAllocation()
{
    if (m_bufferAllocation)
        return m_bufferAllocation;

    RP_CORE_ERROR("Buffer::getBufferAllocation() called on a non-pooled buffer!");
    return nullptr;
}

void Buffer::setBufferAllocation(std::shared_ptr<BufferAllocation> allocation)
{
    m_bufferAllocation = allocation;
}

void Buffer::createBuffer()
{
    if (m_bufferAllocation) {
        RP_CORE_ERROR("createBuffer() called on a pooled buffer!");
        return;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_Size;
    bufferInfo.usage = m_usageFlags;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};

    // Set VMA usage flags based on Vulkan memory properties
    if ((m_propertiesFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (m_propertiesFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        // Used for staging buffers or CPU-writable/readable buffers that the GPU also reads
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    } else if (m_propertiesFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        // Host visible, but maybe not coherent.
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    } else if (m_propertiesFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
        // Device local, not CPU accessible directly
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    } else {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    }

    if (vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, nullptr) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to create buffer!");
        throw std::runtime_error("failed to create buffer!");
    }
}
} // namespace Rapture
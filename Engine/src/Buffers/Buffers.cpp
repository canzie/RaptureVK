#include "Buffers.h"


#include "Buffers/CommandBuffers/CommandPool.h"
#include "WindowContext/Application.h"

#include "Logging/Log.h"
#include "stdexcept"

namespace Rapture {

    Buffer::Buffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator)
    : m_Allocator(allocator), m_usage(usage), m_Size(size)
    {

    }

    Buffer::~Buffer() {
        destoryObjects();
    }

    void Buffer::destoryObjects()
    {
        vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);
    }

    void Buffer::addData(void *newData, VkDeviceSize size, VkDeviceSize offset)
    {
        // Check for buffer overflow
        if (offset + size > m_Size) {
            RP_CORE_ERROR("Buffer::addData - Buffer overflow detected! Attempted to write {} bytes at offset {} in buffer of size {}", size, offset, m_Size);
            return;
        }

        if (!(m_propertiesFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            RP_CORE_ERROR("Buffer::addData - Buffer is not host visible! Use addDataGPU for device local buffers.");
            return;
        }
    
        void* mappedData;
        if (vmaMapMemory(m_Allocator, m_Allocation, &mappedData) != VK_SUCCESS) {
            RP_CORE_ERROR("Buffer::addData - Failed to map memory!");
            return;
        }

        // Copy the data
        char* dst = (char*)mappedData + offset;
        memcpy(dst, newData, size);

        // If memory is not host coherent, we need to flush
        if (!(m_propertiesFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            vmaFlushAllocation(m_Allocator, m_Allocation, offset, size);
        }

        vmaUnmapMemory(m_Allocator, m_Allocation);
    }


    void Buffer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
    {
        auto& app = Application::getInstance();
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
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer->getCommandBufferVk(), srcBuffer, dstBuffer, 1, &copyRegion);

        commandBuffer->end();
        // Submit command buffer

        auto& vulkanContext = app.getVulkanContext();
    
        auto queue = vulkanContext.getGraphicsQueue();
        //queue->addCommandBuffer(commandBuffer);
        queue->submitQueue(commandBuffer, VK_NULL_HANDLE);
        queue->waitIdle();
        
    }

    VkDescriptorBufferInfo Buffer::getDescriptorBufferInfo() const {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_Buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = m_Size;
        return bufferInfo;
    }

    void Buffer::createBuffer()
    { 
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
}
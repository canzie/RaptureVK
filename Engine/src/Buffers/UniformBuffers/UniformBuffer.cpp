#include "UniformBuffer.h"

#include "WindowContext/Application.h"
#include "Logging/Log.h"

namespace Rapture {



UniformBuffer::UniformBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, void* data)
    : Buffer(size, usage, allocator)
{
    
    m_usageFlags = getBufferUsage();
    m_propertiesFlags = getMemoryPropertyFlags();
        
    createBuffer();

    if (data && m_propertiesFlags == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        addData(data, size, 0);
    } else if (data && m_propertiesFlags == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
        addDataGPU(data, size, 0);
    }
}

UniformBuffer::~UniformBuffer() {
}


void UniformBuffer::createDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorPool descriptorPool, uint32_t binding)
{

    auto& app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_Buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = m_Size;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;

    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;
    descriptorWrite.pImageInfo = nullptr; // Optional
    descriptorWrite.pTexelBufferView = nullptr; // Optional

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    
}

VkBufferUsageFlags UniformBuffer::getBufferUsage()
{
    switch (m_usage) {
        case BufferUsage::STATIC:
            return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        case BufferUsage::DYNAMIC:
            return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        case BufferUsage::STREAM:
            return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        case BufferUsage::STAGING:
            return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; // fallback}
}

VkMemoryPropertyFlags UniformBuffer::getMemoryPropertyFlags()
{
    switch (m_usage) {
        case BufferUsage::STATIC:
            return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        case BufferUsage::DYNAMIC:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        case BufferUsage::STREAM:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        case BufferUsage::STAGING:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT; // fallback
}

void UniformBuffer::addDataGPU(void *data, VkDeviceSize size, VkDeviceSize offset)
{
        // Check for buffer overflow
    if (offset + size > m_Size) {
        RP_CORE_ERROR("UniformBuffer::addDataGPU - Buffer overflow detected! Attempted to write {} bytes at offset {} in buffer of size {}", size, offset, m_Size);
        return;
    }

    // Create a staging buffer
    UniformBuffer stagingBuffer(size, BufferUsage::STAGING, m_Allocator);


    // Copy data to staging buffer
    stagingBuffer.addData(data, size, 0);

    // Copy from staging buffer to device local buffer
    copyBuffer(stagingBuffer.getBufferVk(), m_Buffer, size);
}
}

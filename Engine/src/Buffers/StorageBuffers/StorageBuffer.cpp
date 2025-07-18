#include "StorageBuffer.h"

#include "WindowContext/Application.h"
#include "Logging/Log.h"

#include "Buffers/Descriptors/DescriptorManager.h"

namespace Rapture {



StorageBuffer::StorageBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, void* data)
    : Buffer(size, usage, allocator)
{
    
    m_usageFlags = getBufferUsage();
    m_propertiesFlags = getMemoryPropertyFlags();
        
    createBuffer();

    if (data && (usage == BufferUsage::DYNAMIC || usage == BufferUsage::STREAM)) {
        addData(data, size, 0);
    } else if (data && usage == BufferUsage::STATIC) {
        addDataGPU(data, size, 0);
    }
}

StorageBuffer::StorageBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, VkBufferUsageFlags additionalUsageFlags, void* data)
    : Buffer(size, usage, allocator)
{
    
    m_usageFlags = getBufferUsage() | additionalUsageFlags;
    m_propertiesFlags = getMemoryPropertyFlags();
        
    createBuffer();

    if (data && (usage == BufferUsage::DYNAMIC || usage == BufferUsage::STREAM)) {
        addData(data, size, 0);
    } else if (data && usage == BufferUsage::STATIC) {
        addDataGPU(data, size, 0);
    }
}

StorageBuffer::~StorageBuffer() {

    if (m_bindlessIndex != UINT32_MAX) {
        auto set = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::BINDLESS_SSBOS);
        if (set) {
            auto binding = set->getSSBOBinding(DescriptorSetBindingLocation::BINDLESS_SSBOS);
            if (binding) {
                binding->free(m_bindlessIndex);
            }
        }
    }

}



VkBufferUsageFlags StorageBuffer::getBufferUsage()
{
    switch (m_usage) {
        case BufferUsage::STATIC:
            return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        case BufferUsage::DYNAMIC:
            return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        case BufferUsage::STREAM:
            return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        case BufferUsage::STAGING:
            return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; // fallback}
}

VkMemoryPropertyFlags StorageBuffer::getMemoryPropertyFlags()
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

void StorageBuffer::addDataGPU(void *data, VkDeviceSize size, VkDeviceSize offset)
{
        // Check for buffer overflow
    if (offset + size > m_Size) {
        RP_CORE_ERROR("StorageBuffer::addDataGPU - Buffer overflow detected! Attempted to write {} bytes at offset {} in buffer of size {}", size, offset, m_Size);
        return;
    }

    // Create a staging buffer
    StorageBuffer stagingBuffer(size, BufferUsage::STAGING, m_Allocator);


    // Copy data to staging buffer
    stagingBuffer.addData(data, size, 0);

    // Copy from staging buffer to device local buffer
    copyBuffer(stagingBuffer.getBufferVk(), m_Buffer, size);
}
uint32_t StorageBuffer::getBindlessIndex() {
    if (m_bindlessIndex != UINT32_MAX) {
        return m_bindlessIndex;
    }

    auto set = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::BINDLESS_SSBOS);
    if (set) {
        auto binding = set->getSSBOBinding(DescriptorSetBindingLocation::BINDLESS_SSBOS);

        if (binding){
            m_bindlessIndex = binding->add(shared_from_this());
        }

    }

    return m_bindlessIndex;
}
}

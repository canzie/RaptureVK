#include "IndexBuffer.h"
#include "Logging/Log.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Utils/GLTypes.h"

namespace Rapture {

std::shared_ptr<DescriptorBindingSSBO> IndexBuffer::s_bindlessBuffers = nullptr;

VkIndexType getIndexTypeVk(uint32_t indexType) {
    switch (indexType) {
        case UNSIGNED_SHORT_TYPE:
            return VK_INDEX_TYPE_UINT16;
        case UNSIGNED_INT_TYPE: // UNSIGNED_INT
            return VK_INDEX_TYPE_UINT32;
        case UNSIGNED_BYTE_TYPE: // UNSIGNED_BYTE
            return VK_INDEX_TYPE_UINT8;
        default:
            return VK_INDEX_TYPE_UINT16; // Default to uint32
    }
}

IndexBuffer::IndexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, VkIndexType indexType)
    : Buffer(size, usage, allocator), m_indexType(indexType)
{

    m_usageFlags = getBufferUsage();
    m_propertiesFlags = getMemoryPropertyFlags();
        
    createBuffer();
}   

IndexBuffer::IndexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, uint32_t indexType)
    : IndexBuffer(size, usage, allocator, getIndexTypeVk(indexType)) {}   

IndexBuffer::~IndexBuffer()
{
    // Free the bindless descriptor if allocated
    if (m_bindlessIndex != UINT32_MAX && s_bindlessBuffers) {
        s_bindlessBuffers->free(m_bindlessIndex);
    }
}   

VkBufferUsageFlags IndexBuffer::getBufferUsage() {
    switch (m_usage) {
        case BufferUsage::STATIC:
            return VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; // Added for bindless access
        case BufferUsage::DYNAMIC:
            return VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        case BufferUsage::STREAM:
            return VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        case BufferUsage::STAGING:
            return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    return VK_BUFFER_USAGE_INDEX_BUFFER_BIT; // fallback
}

VkMemoryPropertyFlags IndexBuffer::getMemoryPropertyFlags() {
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

uint32_t IndexBuffer::getBindlessIndex()
{
    if (m_bindlessIndex != UINT32_MAX) {
        return m_bindlessIndex;
    }
    
    // Initialize the bindless buffer pool if not already done
    if (s_bindlessBuffers == nullptr) {
        auto set = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::BINDLESS_SSBOS);
        if (set) {
            s_bindlessBuffers = set->getSSBOBinding(DescriptorSetBindingLocation::BINDLESS_SSBOS);
        }
    }
    
    if (s_bindlessBuffers) {
        // For now, we'll use a placeholder index based on buffer address
        m_bindlessIndex = s_bindlessBuffers->add(shared_from_this());
    }
    
    return m_bindlessIndex;
}

void IndexBuffer::addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset) {
    // Check for buffer overflow
    if (offset + size > m_Size) {
        RP_CORE_ERROR("IndexBuffer::addDataGPU - Buffer overflow detected! Attempted to write {} bytes at offset {} in buffer of size {}", size, offset, m_Size);
        return;
    }

    // Create a staging buffer
    IndexBuffer stagingBuffer(size, BufferUsage::STAGING, m_Allocator, m_indexType);


    // Copy data to staging buffer
    stagingBuffer.addData(data, size, 0);

    // Copy from staging buffer to device local buffer
    copyBuffer(stagingBuffer.getBufferVk(), m_Buffer, size);
}

}


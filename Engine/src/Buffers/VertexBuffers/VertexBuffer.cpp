#include "VertexBuffer.h"
#include "Logging/Log.h"

namespace Rapture {

VertexBuffer::VertexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator)
    : Buffer(size, usage, allocator)
{

        m_usageFlags = getBufferUsage();
        m_propertiesFlags = getMemoryPropertyFlags();
        
        createBuffer();
}

VertexBuffer::~VertexBuffer(){

}

VkBufferUsageFlags VertexBuffer::getBufferUsage() {
    switch (m_usage) {
        case BufferUsage::STATIC:
            return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        case BufferUsage::DYNAMIC:
            return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        case BufferUsage::STREAM:
            return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        case BufferUsage::STAGING:
            return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; // fallback

}

VkMemoryPropertyFlags VertexBuffer::getMemoryPropertyFlags() {

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

void VertexBuffer::setBufferLayout(const BufferLayout& bufferLayout)
{
    m_bufferLayout = bufferLayout;
}


void VertexBuffer::addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset)
{
    // Check for buffer overflow
    if (offset + size > m_Size) {
        RP_CORE_ERROR("VertexBuffer::addDataGPU - Buffer overflow detected! Attempted to write {} bytes at offset {} in buffer of size {}", size, offset, m_Size);
        return;
    }

    // Create a staging buffer
    VertexBuffer stagingBuffer(size, BufferUsage::STAGING, m_Allocator);


    // Copy data to staging buffer
    stagingBuffer.addData(data, size, 0);

    // Copy from staging buffer to device local buffer
    copyBuffer(stagingBuffer.getBufferVk(), m_Buffer, size);

}



}
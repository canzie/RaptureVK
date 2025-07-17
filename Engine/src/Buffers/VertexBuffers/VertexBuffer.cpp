#include "VertexBuffer.h"
#include "Logging/Log.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Buffers/BufferPool.h"
#include "WindowContext/Application.h"

namespace Rapture {

std::shared_ptr<DescriptorBindingSSBO> VertexBuffer::s_bindlessBuffers = nullptr;

VertexBuffer::VertexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator)
    : Buffer(size, usage, allocator)
{

        m_usageFlags = getBufferUsage();
        m_propertiesFlags = getMemoryPropertyFlags();
        
        createBuffer();
}

VertexBuffer::VertexBuffer(BufferAllocationRequest& request, VmaAllocator allocator, void* data)
    : Buffer(request.size, request.usage, allocator), m_bufferLayout(request.layout)
{
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto& bufferPoolManager = BufferPoolManager::getInstance();

    request.type = BufferType::VERTEX;
    m_bufferAllocation = bufferPoolManager.allocateBuffer(request);

    if (!m_bufferAllocation)
    {
        RP_CORE_ERROR("VertexBuffer::VertexBuffer - Failed to allocate buffer!");
        throw std::runtime_error("VertexBuffer::VertexBuffer - Failed to allocate buffer!");
    }

    if (m_bufferAllocation && data)
    {
        m_bufferAllocation->uploadData(data, request.size);
    }


}

VertexBuffer::~VertexBuffer(){
    // Free the bindless descriptor if allocated
    if (m_bindlessIndex != UINT32_MAX && s_bindlessBuffers) {
        s_bindlessBuffers->free(m_bindlessIndex);
    }
    if (m_bufferAllocation) {
        m_bufferAllocation->free();
    }
}

VkBufferUsageFlags VertexBuffer::getBufferUsage() {
    switch (m_usage) {
        case BufferUsage::STATIC:
            return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; // Added for bindless access
        case BufferUsage::DYNAMIC:
            return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        case BufferUsage::STREAM:
            return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
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

uint32_t VertexBuffer::getBindlessIndex()
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

void VertexBuffer::addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset)
{
    // Check for buffer overflow
    if (offset + size > getSize()) {
        RP_CORE_ERROR("VertexBuffer::addDataGPU - Buffer overflow detected! Attempted to write {} bytes at offset {} in buffer of size {}", size, offset, getSize());
        return;
    }

    if (m_bufferAllocation)
    {
		m_bufferAllocation->uploadData(data, size, offset);
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
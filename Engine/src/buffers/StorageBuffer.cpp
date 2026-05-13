#include "StorageBuffer.h"

#include "logging/Log.h"
#include "window_context/Application.h"

#include "buffers/descriptors/DescriptorManager.h"

namespace Rapture {

StorageBuffer::StorageBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, void *data)
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

StorageBuffer::StorageBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, VkBufferUsageFlags additionalUsageFlags,
                             void *data)
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

StorageBuffer::~StorageBuffer()
{

    if (m_bindlessIndex != UINT32_MAX) {
        auto& rc = Application::getInstance().getVulkanContext().getRenderContext();
        auto set = rc.descriptorManager->getDescriptorSet(DescriptorSetBindingLocation::BINDLESS_SSBOS);
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
    return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
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
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
}

void StorageBuffer::addDataGPU(void *data, VkDeviceSize size, VkDeviceSize offset)
{
    if (offset + size > m_Size) {
        RP_CORE_ERROR("Buffer overflow detected! Attempted to write {} bytes at offset {} in buffer of size {}", size, offset,
                      m_Size);
        return;
    }

    StorageBuffer stagingBuffer(size, BufferUsage::STAGING, m_Allocator);

    stagingBuffer.addData(data, size, 0);

    copyBuffer(stagingBuffer.getBufferVk(), m_Buffer, size);
}
uint32_t StorageBuffer::getBindlessIndex()
{
    if (m_bindlessIndex != UINT32_MAX) {
        return m_bindlessIndex;
    }

    auto& rc = Application::getInstance().getVulkanContext().getRenderContext();
    auto set = rc.descriptorManager->getDescriptorSet(DescriptorSetBindingLocation::BINDLESS_SSBOS);
    if (set) {
        auto binding = set->getSSBOBinding(DescriptorSetBindingLocation::BINDLESS_SSBOS);

        if (binding) {
            m_bindlessIndex = binding->add(*this);
        }
    }

    return m_bindlessIndex;
}
} // namespace Rapture

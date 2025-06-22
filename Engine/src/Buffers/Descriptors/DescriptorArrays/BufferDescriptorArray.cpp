#include "BufferDescriptorArray.h"
#include "WindowContext/Application.h"

#include "Logging/Log.h"
#include "Buffers/StorageBuffers/StorageBuffer.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"

#include <stdexcept>

namespace Rapture {

// BufferDescriptorSubAllocation Implementation
BufferDescriptorSubAllocation::~BufferDescriptorSubAllocation() {
    for (uint32_t i = 0; i < m_capacity; ++i) {
        if (m_isIndexUsed[i]) {
            m_parent->free(m_startIndex + i);
        }
    }
    RP_CORE_INFO("Destroyed and freed storage descriptor sub-allocation of size {} at index {}", m_capacity, m_startIndex);
}

// BufferDescriptorArray Implementation
BufferDescriptorArray::BufferDescriptorArray(const DescriptorArrayConfig& config, VkDescriptorSet set)
    : DescriptorArrayBase<Buffer>(config, set) {
    
    if (m_capacity == 0) {
        return;
    }

    auto& app = Application::getInstance();
    m_device = app.getVulkanContext().getLogicalDevice();
    m_isIndexUsed.resize(m_capacity, false);

    m_defaultResource = createDefaultResource();


    initializeSlotsWithDefault();

    RP_CORE_INFO("Created BufferDescriptorArray with capacity {} for type {}", m_capacity, getDescriptorArrayTypeName(config.arrayType));
}

std::unique_ptr<DescriptorSubAllocationBase<Buffer>> BufferDescriptorArray::createSubAllocation(uint32_t capacity, std::string name) {
    uint32_t startIndex = UINT32_MAX;
    uint32_t consecutiveFreeCount = 0;
    
    for (uint32_t i = 0; i < m_capacity; ++i) {
        if (!m_isIndexUsed[i]) {
            if (consecutiveFreeCount == 0) {
                startIndex = i;
            }
            consecutiveFreeCount++;
        } else {
            consecutiveFreeCount = 0;
        }

        if (consecutiveFreeCount == capacity) {
            // Found a block, mark as used and create sub-allocator
            for(uint32_t j = 0; j < capacity; ++j) {
                m_isIndexUsed[startIndex + j] = true;
            }
            RP_CORE_INFO("Allocated a storage descriptor sub-block of size {} at index {}", capacity, startIndex);
            return std::make_unique<BufferDescriptorSubAllocation>(this, startIndex, capacity, name);
        }
    }

    RP_CORE_ERROR("Failed to find a contiguous block of size {} for a storage descriptor sub-allocation. Name: {}", capacity, name);
    return nullptr;
}

uint32_t BufferDescriptorArray::allocate(std::shared_ptr<Buffer> resource) {
    for (uint32_t i = 0; i < m_capacity; ++i) {
        uint32_t index = (m_nextFreeIndex + i) % m_capacity;
        if (!m_isIndexUsed[index]) {
            m_isIndexUsed[index] = true;
            m_nextFreeIndex = (index + 1) % m_capacity;
            update(index, resource);
            return index;
        }
    }

    RP_CORE_ERROR("BufferDescriptorArray is full! Failed to allocate a new handle.");
    return UINT32_MAX;
}

void BufferDescriptorArray::update(uint32_t index, std::shared_ptr<Buffer> resource) {
    if (index >= m_capacity) {
        RP_CORE_WARN("Attempted to update a storage descriptor at an out-of-bounds index: {}", index);
        return;
    }
    if (!resource) {
        RP_CORE_WARN("Attempted to update storage descriptor at index {} with a null buffer. Binding default buffer instead.", index);
        update(index, m_defaultResource);
        return;
    }

    // Check if the buffer has the correct usage flags for this descriptor type
    VkBufferUsageFlags bufferUsage = resource->getBufferUsage();
    bool isValidForType = false;
    
    if (m_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
        isValidForType = (bufferUsage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0;
    } else if (m_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
        isValidForType = (bufferUsage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) != 0;
    }
    
    if (!isValidForType) {
        RP_CORE_WARN("Buffer at index {} does not have the correct usage flags for descriptor type {}. Using default buffer instead.", 
                     index, static_cast<int>(m_type));
        update(index, m_defaultResource);
        return;
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = resource->getBufferVk();
    bufferInfo.offset = 0;
    bufferInfo.range = resource->getSize();

    VkWriteDescriptorSet writeSet{};
    writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSet.dstSet = m_set;
    writeSet.dstBinding = m_bindingIndex;
    writeSet.dstArrayElement = index;
    writeSet.descriptorType = m_type;
    writeSet.descriptorCount = 1;
    writeSet.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_device, 1, &writeSet, 0, nullptr);
}

void BufferDescriptorArray::free(uint32_t index) {
    if (index >= m_capacity) {
        RP_CORE_WARN("Attempted to free an out-of-bounds storage descriptor handle: {}", index);
        return;
    }

    if (m_isIndexUsed[index]) {
        m_isIndexUsed[index] = false;
        m_nextFreeIndex = index;
        // Update the slot back to the default buffer to avoid dangling references.
        update(index, m_defaultResource);
    }
}



void BufferDescriptorArray::initializeSlotsWithDefault() {
    if (!m_defaultResource) {
        RP_CORE_WARN("Cannot initialize BufferDescriptorArray slots: default buffer is null.");
        return;
    }

    VkDescriptorBufferInfo defaultBufferInfo{};
    defaultBufferInfo.buffer = m_defaultResource->getBufferVk();
    defaultBufferInfo.offset = 0;
    defaultBufferInfo.range = m_defaultResource->getSize();
    
    std::vector<VkDescriptorBufferInfo> bufferInfos(m_capacity, defaultBufferInfo);

    VkWriteDescriptorSet writeSet{};
    writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSet.dstSet = m_set;
    writeSet.dstBinding = m_bindingIndex;
    writeSet.dstArrayElement = 0;
    writeSet.descriptorType = m_type;
    writeSet.descriptorCount = m_capacity;
    writeSet.pBufferInfo = bufferInfos.data();

    vkUpdateDescriptorSets(m_device, 1, &writeSet, 0, nullptr);
}

std::shared_ptr<Buffer> BufferDescriptorArray::createDefaultResource() {
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    VmaAllocator allocator = vulkanContext.getVmaAllocator();
    
    // Create a small default buffer (16 bytes) with the appropriate usage flags
    VkDeviceSize defaultSize = 16;
    
    if (m_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
        return std::make_shared<UniformBuffer>(defaultSize, BufferUsage::STATIC, allocator);
    } 
    
    // Default to storage buffer for VK_DESCRIPTOR_TYPE_STORAGE_BUFFER or any other type
    return std::make_shared<StorageBuffer>(defaultSize, BufferUsage::STATIC, allocator);
}

} // namespace Rapture 
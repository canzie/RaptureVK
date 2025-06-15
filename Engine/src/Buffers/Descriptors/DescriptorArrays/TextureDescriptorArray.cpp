#include "TextureDescriptorArray.h"
#include "WindowContext/Application.h"
#include "Logging/Log.h"
#include "AssetManager/AssetManager.h"

#include <stdexcept>

namespace Rapture {

// TextureDescriptorSubAllocation Implementation
TextureDescriptorSubAllocation::~TextureDescriptorSubAllocation() {
    for (uint32_t i = 0; i < m_capacity; ++i) {
        if (m_isIndexUsed[i]) {
            m_parent->free(m_startIndex + i);
        }
    }
    RP_CORE_INFO("Destroyed and freed texture descriptor sub-allocation of size {} at index {}", m_capacity, m_startIndex);
}

// TextureDescriptorArray Implementation
TextureDescriptorArray::TextureDescriptorArray(const DescriptorArrayConfig& config, VkDescriptorSet set)
    : DescriptorArrayBase<Texture>(config, set) {
    
    if (m_capacity == 0) {
        return;
    }

    auto& app = Application::getInstance();
    m_device = app.getVulkanContext().getLogicalDevice();
    m_isIndexUsed.resize(m_capacity, false);

    m_defaultResource = createDefaultResource();
    initializeSlotsWithDefault();

    RP_CORE_INFO("Created TextureDescriptorArray with capacity {} for type {}", m_capacity, static_cast<int>(m_type));
}

std::unique_ptr<DescriptorSubAllocationBase<Texture>> TextureDescriptorArray::createSubAllocation(uint32_t capacity, std::string name) {
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
            RP_CORE_INFO("Allocated a texture descriptor sub-block of size {} at index {}", capacity, startIndex);
            return std::make_unique<TextureDescriptorSubAllocation>(this, startIndex, capacity, name);
        }
    }

    RP_CORE_ERROR("Failed to find a contiguous block of size {} for a texture descriptor sub-allocation.", capacity);
    return nullptr;
}

uint32_t TextureDescriptorArray::allocate(std::shared_ptr<Texture> resource) {
    for (uint32_t i = 0; i < m_capacity; ++i) {
        uint32_t index = (m_nextFreeIndex + i) % m_capacity;
        if (!m_isIndexUsed[index]) {
            m_isIndexUsed[index] = true;
            m_nextFreeIndex = (index + 1) % m_capacity;
            update(index, resource);
            return index;
        }
    }

    RP_CORE_ERROR("TextureDescriptorArray is full! Failed to allocate a new handle.");
    return UINT32_MAX;
}

void TextureDescriptorArray::update(uint32_t index, std::shared_ptr<Texture> resource) {
    if (index >= m_capacity) {
        RP_CORE_WARN("Attempted to update a texture descriptor at an out-of-bounds index: {}", index);
        return;
    }
    if (!resource) {
        RP_CORE_WARN("Attempted to update texture descriptor at index {} with a null texture. Binding default texture instead.", index);
        update(index, m_defaultResource);
        return;
    }

    VkDescriptorImageInfo imageInfo = resource->getDescriptorImageInfo();

    VkWriteDescriptorSet writeSet{};
    writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSet.dstSet = m_set;
    writeSet.dstBinding = m_bindingIndex;
    writeSet.dstArrayElement = index;
    writeSet.descriptorType = m_type;
    writeSet.descriptorCount = 1;
    writeSet.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_device, 1, &writeSet, 0, nullptr);
}

void TextureDescriptorArray::free(uint32_t index) {
    if (index >= m_capacity) {
        RP_CORE_WARN("Attempted to free an out-of-bounds texture descriptor handle: {}", index);
        return;
    }

    if (m_isIndexUsed[index]) {
        m_isIndexUsed[index] = false;
        m_nextFreeIndex = index;
        // Update the slot back to the default texture to avoid dangling references.
        update(index, m_defaultResource);
    }
}



void TextureDescriptorArray::initializeSlotsWithDefault() {
    if (!m_defaultResource) {
        RP_CORE_WARN("Cannot initialize TextureDescriptorArray slots: default texture is null.");
        return;
    }

    VkDescriptorImageInfo defaultImageInfo = m_defaultResource->getDescriptorImageInfo();
    std::vector<VkDescriptorImageInfo> imageInfos(m_capacity, defaultImageInfo);

    VkWriteDescriptorSet writeSet{};
    writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSet.dstSet = m_set;
    writeSet.dstBinding = m_bindingIndex;
    writeSet.dstArrayElement = 0;
    writeSet.descriptorType = m_type;
    writeSet.descriptorCount = m_capacity;
    writeSet.pImageInfo = imageInfos.data();

    vkUpdateDescriptorSets(m_device, 1, &writeSet, 0, nullptr);
}

std::shared_ptr<Texture> TextureDescriptorArray::createDefaultResource() {
    auto [defaultTexture, defaultTextureHandle] = AssetManager::importDefaultAsset<Texture>(AssetType::Texture);
    return defaultTexture;
}

} // namespace Rapture 
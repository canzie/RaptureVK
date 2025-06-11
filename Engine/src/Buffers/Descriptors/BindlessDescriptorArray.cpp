#include "BindlessDescriptorArray.h"
#include "WindowContext/Application.h"
#include "Logging/Log.h"
#include "Textures/Texture.h"
#include "AssetManager/AssetManager.h"

#include <stdexcept>

namespace Rapture {

BindlessDescriptorArray::BindlessDescriptorArray(BindlessDescriptorArrayConfig config)
    : m_type(config.type), m_capacity(config.capacity), m_name(config.name), m_setBindingIndex(config.setBindingIndex), m_bindingIndex(config.bindingIndex) {

    if (m_capacity == 0) {
        return;
    }

    auto& app = Application::getInstance();
    m_device = app.getVulkanContext().getLogicalDevice();


    m_isIndexUsed.resize(m_capacity, false);

    // a default texture to initialize all slots. This prevents unbound descriptor errors.
    auto [defaultTexture, defaultTextureHandle] = AssetManager::importDefaultAsset<Texture>(AssetType::Texture);
    m_defaultTexture = defaultTexture;

    createPool();
    createLayout();
    allocateSet();
    initializeSlotsWithDefault();

    RP_CORE_INFO("Created BindlessDescriptorArray with capacity {} for type {}", m_capacity, static_cast<int>(m_type));
}

BindlessDescriptorArray::~BindlessDescriptorArray() {
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
    }
    RP_CORE_INFO("Destroyed BindlessDescriptorArray.");
}

void BindlessDescriptorArray::createPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = m_type;
    poolSize.descriptorCount = m_capacity;

    // Get the device features
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto descriptorIndexingFeatures = vulkanContext.getDescriptorIndexingFeatures();
    
    VkDescriptorPoolCreateFlags poolFlags = 0;
    
    // Only use UPDATE_AFTER_BIND if the corresponding feature is enabled for this descriptor type
    bool canUseUpdateAfterBind = descriptorIndexingFeatures.descriptorBindingUpdateUnusedWhilePending &&
        ((m_type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && 
          descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind) ||
         (m_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE && 
          descriptorIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind) ||
         (m_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && 
          descriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind));
    
    if (canUseUpdateAfterBind) {
        poolFlags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        RP_CORE_INFO("Using VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT for bindless array pool");
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = poolFlags;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create descriptor pool for BindlessDescriptorArray.");
        throw std::runtime_error("Failed to create descriptor pool for BindlessDescriptorArray.");
    }
}

void BindlessDescriptorArray::createLayout() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0; // The array is at binding 0
    binding.descriptorType = m_type;
    binding.descriptorCount = m_capacity;
    binding.stageFlags = VK_SHADER_STAGE_ALL; // Accessible from any shader stage

    // Get the device features
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto descriptorIndexingFeatures = vulkanContext.getDescriptorIndexingFeatures();
    
    VkDescriptorBindingFlags bindingFlags = 0;
    bool useUpdateAfterBind = false;
    
    // Only use these flags if the corresponding features are enabled
    if (descriptorIndexingFeatures.descriptorBindingPartiallyBound) {
        bindingFlags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
        RP_CORE_INFO("Using VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT for bindless array");
    }
    
    bool canUseUpdateAfterBind = false;
    if (descriptorIndexingFeatures.descriptorBindingUpdateUnusedWhilePending) {
        if ((m_type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && 
             descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind) ||
            (m_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE && 
             descriptorIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind) ||
            (m_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && 
             descriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind)) {
            bindingFlags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            useUpdateAfterBind = true;
            RP_CORE_INFO("Using VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT for bindless array");
        }
    }

    // Only include binding flags extension if we're actually using extended flags
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    
    // Only set up extended binding flags if we're using any
    VkDescriptorSetLayoutBindingFlagsCreateInfo extendedInfo{};
    if (bindingFlags != 0) {
        extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        extendedInfo.bindingCount = 1;
        extendedInfo.pBindingFlags = &bindingFlags;
        layoutInfo.pNext = &extendedInfo;
        
        if (useUpdateAfterBind) {
            layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        }
    }

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create descriptor set layout for BindlessDescriptorArray.");
        throw std::runtime_error("Failed to create descriptor set layout for BindlessDescriptorArray.");
    }
}

void BindlessDescriptorArray::allocateSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_layout;

    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_set) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to allocate descriptor set for BindlessDescriptorArray.");
        throw std::runtime_error("Failed to allocate descriptor set for BindlessDescriptorArray.");
    }
}

void BindlessDescriptorArray::initializeSlotsWithDefault() {
    if (!m_defaultTexture) {
        RP_CORE_WARN("Cannot initialize BindlessDescriptorArray slots: default texture is null.");
        return;
    }

    VkDescriptorImageInfo defaultImageInfo = m_defaultTexture->getDescriptorImageInfo();
    std::vector<VkDescriptorImageInfo> imageInfos(m_capacity, defaultImageInfo);

    VkWriteDescriptorSet writeSet{};
    writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSet.dstSet = m_set;
    writeSet.dstBinding = 0;
    writeSet.dstArrayElement = 0;
    writeSet.descriptorType = m_type;
    writeSet.descriptorCount = m_capacity;
    writeSet.pImageInfo = imageInfos.data();

    vkUpdateDescriptorSets(m_device, 1, &writeSet, 0, nullptr);
}

uint32_t BindlessDescriptorArray::allocateSingle(std::shared_ptr<Texture> texture) {
    for (uint32_t i = 0; i < m_capacity; ++i) {
        uint32_t index = (m_nextFreeIndex + i) % m_capacity;
        if (!m_isIndexUsed[index]) {
            m_isIndexUsed[index] = true;
            m_nextFreeIndex = (index + 1) % m_capacity;
            update(index, texture);
            return index;
        }
    }

    RP_CORE_ERROR("BindlessDescriptorArray is full! Failed to allocate a new handle.");
    return UINT32_MAX;
}

std::unique_ptr<BindlessDescriptorSubAllocation> BindlessDescriptorArray::createSubAllocation(uint32_t capacity, std::string name) {
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
            RP_CORE_INFO("Allocated a bindless sub-block of size {} at index {}", capacity, startIndex);
            return std::make_unique<BindlessDescriptorSubAllocation>(this, startIndex, capacity, name);
        }
    }

    RP_CORE_ERROR("Failed to find a contiguous block of size {} for a bindless sub-allocation.", capacity);
    return nullptr;
}

void BindlessDescriptorArray::update(uint32_t index, std::shared_ptr<Texture> texture) {
    if (index >= m_capacity) {
        RP_CORE_WARN("Attempted to update a bindless descriptor at an out-of-bounds index: {}", index);
        return;
    }
    if (!texture) {
        RP_CORE_WARN("Attempted to update bindless descriptor at index {} with a null texture. Binding default texture instead.", index);
        update(index, m_defaultTexture);
        return;
    }

    VkDescriptorImageInfo imageInfo = texture->getDescriptorImageInfo();

    VkWriteDescriptorSet writeSet{};
    writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSet.dstSet = m_set;
    writeSet.dstBinding = 0;
    writeSet.dstArrayElement = index;
    writeSet.descriptorType = m_type;
    writeSet.descriptorCount = 1;
    writeSet.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_device, 1, &writeSet, 0, nullptr);
}

void BindlessDescriptorArray::free(uint32_t index) {
    if (index >= m_capacity) {
        RP_CORE_WARN("Attempted to free an out-of-bounds bindless handle: {}", index);
        return;
    }

    if (m_isIndexUsed[index]) {
        m_isIndexUsed[index] = false;
        m_nextFreeIndex = index; // Start searching from the last freed spot.
        // Update the slot back to the default texture to avoid dangling references.
        update(index, m_defaultTexture);
    }
}

// ------------------------------------
// BindlessDescriptorSubAllocation
// ------------------------------------

BindlessDescriptorSubAllocation::BindlessDescriptorSubAllocation(BindlessDescriptorArray* parent, uint32_t startIndex, uint32_t capacity, std::string name)
    : m_parent(parent), m_startIndex(startIndex), m_capacity(capacity), m_nextFreeIndex(0), m_name(name), m_freeCount(capacity) {
    m_isIndexUsed.resize(capacity, false);
}

BindlessDescriptorSubAllocation::~BindlessDescriptorSubAllocation() {
    for (uint32_t i = 0; i < m_capacity; ++i) {
        if (m_isIndexUsed[i]) {
            // We only need to free the parent index, the parent will update the texture to default
            m_parent->free(m_startIndex + i);
        }
    }
    RP_CORE_INFO("Destroyed and freed bindless sub-allocation of size {} at index {}", m_capacity, m_startIndex);
}

uint32_t BindlessDescriptorSubAllocation::allocate(std::shared_ptr<Texture> texture) {
    for (uint32_t i = 0; i < m_capacity; ++i) {
        uint32_t relativeIndex = (m_nextFreeIndex + i) % m_capacity;
        if (!m_isIndexUsed[relativeIndex]) {
            m_isIndexUsed[relativeIndex] = true;
            m_nextFreeIndex = (relativeIndex + 1) % m_capacity;

            uint32_t absoluteIndex = m_startIndex + relativeIndex;
            m_parent->update(absoluteIndex, texture);
            m_freeCount--;
            return absoluteIndex;
        }
    }

    RP_CORE_ERROR("BindlessDescriptorSubAllocation is full! Failed to allocate a new handle.");
    return UINT32_MAX;
}

void BindlessDescriptorSubAllocation::update(uint32_t index, std::shared_ptr<Texture> texture) {
    if (index < m_startIndex || index >= (m_startIndex + m_capacity)) {
        RP_CORE_WARN("Attempted to update a bindless descriptor at index {} which is out of range for this sub-allocation (start: {}, capacity: {}).", index, m_startIndex, m_capacity);
        return;
    }
    m_parent->update(index, texture);
}

void BindlessDescriptorSubAllocation::free(uint32_t index) {
    if (index < m_startIndex || index >= (m_startIndex + m_capacity)) {
        RP_CORE_WARN("Attempted to free a bindless descriptor at index {} which is out of range for this sub-allocation (start: {}, capacity: {}).", index, m_startIndex, m_capacity);
        return;
    }
    
    uint32_t relativeIndex = index - m_startIndex;
    if (m_isIndexUsed[relativeIndex]) {
        m_isIndexUsed[relativeIndex] = false;
        m_nextFreeIndex = relativeIndex; // Start searching from the last freed spot.
        // Update the slot back to the default texture to avoid dangling references.
        m_parent->update(index, m_parent->getDefaultTexture());
        m_freeCount++;
    }

    if (m_freeCount == m_capacity) { // selfdestruct

    }
}

VkDescriptorSet BindlessDescriptorSubAllocation::getDescriptorSet() const
{
    return m_parent->getSet();
}

} // namespace Rapture 
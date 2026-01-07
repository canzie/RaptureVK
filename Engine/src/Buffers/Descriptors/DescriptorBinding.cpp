#include "DescriptorBinding.h"

#include "AccelerationStructures/TLAS.h"
#include "AssetManager/AssetManager.h"
#include "Buffers/Buffers.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Textures/Texture.h"

#include "Logging/Log.h"
#include "WindowContext/Application.h"

namespace Rapture {

template <typename T>
DescriptorBinding<T>::DescriptorBinding(DescriptorSet *set, uint32_t binding, VkDescriptorType type, uint32_t size)
    : m_set(set), m_binding(binding), m_size(size), m_type(type)
{
    m_isAllocated.resize(size, false);
    m_isArray = size > 1;

    fillEmpty();
}

template <typename T> DescriptorBinding<T>::~DescriptorBinding()
{
    for (uint32_t i = 0; i < m_size; ++i) {
        if (m_isAllocated[i]) {
            free(i);
        }
    }
    m_isAllocated.clear();
}

template <typename T> void DescriptorBinding<T>::fillEmpty() {}

template <typename T> uint32_t DescriptorBinding<T>::findFreeIndex()
{
    for (uint32_t i = 0; i < m_size; ++i) {
        if (!m_isAllocated[i]) {
            return i;
        }
    }
    return UINT32_MAX; // No free index found
}

DescriptorBindingUniformBuffer::DescriptorBindingUniformBuffer(DescriptorSet *set, uint32_t binding, uint32_t size)
    : DescriptorBinding<UniformBuffer>(set, binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, size)
{
}

uint32_t DescriptorBindingUniformBuffer::add(UniformBuffer &resource)
{
    uint32_t index = 0;
    if (m_isArray) {
        index = findFreeIndex();
        if (index == UINT32_MAX) {
            RP_CORE_WARN("No free slots available in array for binding {}", m_binding);
            return UINT32_MAX;
        }
        m_isAllocated[index] = true;
    }

    VkDescriptorBufferInfo bufferInfo = resource.getDescriptorBufferInfo();

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_set->getDescriptorSet();
    descriptorWrite.dstBinding = m_binding;
    descriptorWrite.dstArrayElement = index;
    descriptorWrite.descriptorType = m_type;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    return index;
}

DescriptorBindingTexture::DescriptorBindingTexture(DescriptorSet *set, uint32_t binding, TextureViewType viewType,
                                                   bool isStorageImage, uint32_t size)
    : DescriptorBinding<Texture>(
          set, binding, isStorageImage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, size),
      m_viewType(viewType), m_isStorageImage(isStorageImage)
{
    if (m_isArray && !m_isStorageImage) {
        fillAllSlotsWithPlaceholder();
    }
}

void DescriptorBindingTexture::fillAllSlotsWithPlaceholder()
{
    auto asset = AssetManager::importDefaultAsset(AssetType::TEXTURE);
    auto defaultAsset = asset ? asset.get()->getUnderlyingAsset<Texture>() : nullptr;
    if (!defaultAsset) {
        RP_CORE_ERROR("Failed to get default texture for filling bindless slots");
        return;
    }

    VkDescriptorImageInfo imageInfo = defaultAsset->getDescriptorImageInfo(m_viewType);

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    std::vector<VkWriteDescriptorSet> writes(m_size);
    std::vector<VkDescriptorImageInfo> imageInfos(m_size, imageInfo);

    for (uint32_t i = 0; i < m_size; ++i) {
        writes[i] = {};
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_set->getDescriptorSet();
        writes[i].dstBinding = m_binding;
        writes[i].dstArrayElement = i;
        writes[i].descriptorType = m_type;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &imageInfos[i];
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    RP_CORE_TRACE("Filled {} bindless texture slots with placeholder", m_size);
}

uint32_t DescriptorBindingTexture::add(Texture &resource)
{
    uint32_t index = 0;
    if (m_isArray) {
        index = findFreeIndex();
        if (index == UINT32_MAX) {
            RP_CORE_WARN("No free slots available in array for binding {}", m_binding);
            return UINT32_MAX;
        }
        m_isAllocated[index] = true;
    }

    VkDescriptorImageInfo imageInfo;
    if (m_isStorageImage) {
        imageInfo = resource.getStorageImageDescriptorInfo();
    } else {
        imageInfo = resource.getDescriptorImageInfo(m_viewType);
    }

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_set->getDescriptorSet();
    descriptorWrite.dstBinding = m_binding;
    descriptorWrite.dstArrayElement = index;
    descriptorWrite.descriptorType = m_type;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    return index;
}

DescriptorBindingTLAS::DescriptorBindingTLAS(DescriptorSet *set, uint32_t binding, uint32_t size)
    : DescriptorBinding<TLAS>(set, binding, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, size)
{
}

uint32_t DescriptorBindingTLAS::add(TLAS &resource)
{
    if (resource.getAccelerationStructure() == VK_NULL_HANDLE) {
        RP_CORE_WARN("TLAS is null for binding {}", m_binding);
        return UINT32_MAX;
    }

    uint32_t index = 0;
    if (m_isArray) {
        index = findFreeIndex();
        if (index == UINT32_MAX) {
            RP_CORE_WARN("No free slots available in array for binding {}", m_binding);
            return UINT32_MAX;
        }
        m_isAllocated[index] = true;
    }

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_set->getDescriptorSet();
    descriptorWrite.dstBinding = m_binding;
    descriptorWrite.dstArrayElement = index;
    descriptorWrite.descriptorType = m_type;
    descriptorWrite.descriptorCount = 1;

    // Create acceleration structure write descriptor
    VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureWrite{};
    accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accelerationStructureWrite.accelerationStructureCount = 1;
    VkAccelerationStructureKHR accelerationStructure = resource.getAccelerationStructure();
    accelerationStructureWrite.pAccelerationStructures = &accelerationStructure;

    // Link the acceleration structure write to the main descriptor write
    descriptorWrite.pNext = &accelerationStructureWrite;

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    return index;
}

// Template free method implementation
template <typename T> void DescriptorBinding<T>::free(uint32_t index)
{
    if (m_isArray && index >= m_size) {
        RP_CORE_WARN("Index {} out of bounds for binding {} with size {}", index, m_binding, m_size);
        return;
    }

    if (m_isArray) {
        if (!m_isAllocated[index]) {
            RP_CORE_WARN("Trying to free already free slot {} in binding {}", index, m_binding);
            return;
        }
        m_isAllocated[index] = false;
    }

    // TODO: Write null/empty descriptor to the slot to prevent invalid reads
    // This would require type-specific null descriptor creation
}

// UniformBuffer update method
void DescriptorBindingUniformBuffer::update(UniformBuffer &resource, uint32_t index)
{
    if (m_isArray) {
        if (index >= m_size) {
            RP_CORE_WARN("Index {} out of bounds for binding {} with size {}", index, m_binding, m_size);
            return;
        }
        if (!m_isAllocated[index]) {
            RP_CORE_WARN("Trying to update unallocated slot {} in binding {}", index, m_binding);
            return;
        }
    } else if (index != 0) {
        RP_CORE_WARN("Non-zero index {} specified for non-array binding {}", index, m_binding);
        return;
    }

    VkDescriptorBufferInfo bufferInfo = resource.getDescriptorBufferInfo();

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_set->getDescriptorSet();
    descriptorWrite.dstBinding = m_binding;
    descriptorWrite.dstArrayElement = index;
    descriptorWrite.descriptorType = m_type;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
}

// Texture update method
void DescriptorBindingTexture::update(Texture &resource, uint32_t index)
{
    if (m_isArray) {
        if (index >= m_size) {
            RP_CORE_WARN("Index {} out of bounds for binding {} with size {}", index, m_binding, m_size);
            return;
        }
        if (!m_isAllocated[index]) {
            RP_CORE_WARN("Trying to update unallocated slot {} in binding {}", index, m_binding);
            return;
        }
    } else if (index != 0) {
        RP_CORE_WARN("Non-zero index {} specified for non-array binding {}", index, m_binding);
        return;
    }

    VkDescriptorImageInfo imageInfo;
    if (m_isStorageImage) {
        imageInfo = resource.getStorageImageDescriptorInfo();
    } else {
        imageInfo = resource.getDescriptorImageInfo(m_viewType);
    }

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_set->getDescriptorSet();
    descriptorWrite.dstBinding = m_binding;
    descriptorWrite.dstArrayElement = index;
    descriptorWrite.descriptorType = m_type;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
}

// TLAS update method
void DescriptorBindingTLAS::update(TLAS &resource, uint32_t index)
{
    if (resource.getAccelerationStructure() == VK_NULL_HANDLE) {
        RP_CORE_WARN("TLAS is null for binding {} at index {}", m_binding, index);
        return;
    }

    if (m_isArray) {
        if (index >= m_size) {
            RP_CORE_WARN("Index {} out of bounds for binding {} with size {}", index, m_binding, m_size);
            return;
        }
        if (!m_isAllocated[index]) {
            RP_CORE_WARN("Trying to update unallocated slot {} in binding {}", index, m_binding);
            return;
        }
    } else if (index != 0) {
        RP_CORE_WARN("Non-zero index {} specified for non-array binding {}", index, m_binding);
        return;
    }

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_set->getDescriptorSet();
    descriptorWrite.dstBinding = m_binding;
    descriptorWrite.dstArrayElement = index;
    descriptorWrite.descriptorType = m_type;
    descriptorWrite.descriptorCount = 1;

    // Create acceleration structure write descriptor
    VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureWrite{};
    accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accelerationStructureWrite.accelerationStructureCount = 1;
    VkAccelerationStructureKHR accelerationStructure = resource.getAccelerationStructure();
    accelerationStructureWrite.pAccelerationStructures = &accelerationStructure;

    // Link the acceleration structure write to the main descriptor write
    descriptorWrite.pNext = &accelerationStructureWrite;

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
}

DescriptorBindingSSBO::DescriptorBindingSSBO(DescriptorSet *set, uint32_t binding, uint32_t size)
    : DescriptorBinding<Buffer>(set, binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, size)
{
}

uint32_t DescriptorBindingSSBO::add(Buffer &resource)
{
    uint32_t index = 0;
    if (m_isArray) {
        index = findFreeIndex();
        if (index == UINT32_MAX) {
            RP_CORE_WARN("No free slots available in array for binding {}", m_binding);
            return UINT32_MAX;
        }
        m_isAllocated[index] = true;
    }

    VkDescriptorBufferInfo bufferInfo = resource.getDescriptorBufferInfo();

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_set->getDescriptorSet();
    descriptorWrite.dstBinding = m_binding;
    descriptorWrite.dstArrayElement = index;
    descriptorWrite.descriptorType = m_type;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    return index;
}

// UniformBuffer update method
void DescriptorBindingSSBO::update(Buffer &resource, uint32_t index)
{
    if (m_isArray) {
        if (index >= m_size) {
            RP_CORE_WARN("Index {} out of bounds for binding {} with size {}", index, m_binding, m_size);
            return;
        }
        if (!m_isAllocated[index]) {
            RP_CORE_WARN("Trying to update unallocated slot {} in binding {}", index, m_binding);
            return;
        }
    } else if (index != 0) {
        RP_CORE_WARN("Non-zero index {} specified for non-array binding {}", index, m_binding);
        return;
    }

    VkDescriptorBufferInfo bufferInfo = resource.getDescriptorBufferInfo();

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_set->getDescriptorSet();
    descriptorWrite.dstBinding = m_binding;
    descriptorWrite.dstArrayElement = index;
    descriptorWrite.descriptorType = m_type;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
}

// Explicit template instantiations for the template class DescriptorBinding
template class DescriptorBinding<UniformBuffer>;
template class DescriptorBinding<Texture>;
template class DescriptorBinding<TLAS>;
template class DescriptorBinding<Buffer>;

} // namespace Rapture

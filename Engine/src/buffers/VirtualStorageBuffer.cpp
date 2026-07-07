#include "VirtualStorageBuffer.h"

#include "buffers/StorageBuffer.h"
#include "buffers/descriptors/DescriptorBinding.h"
#include "buffers/descriptors/DescriptorManager.h"
#include "logging/Log.h"
#include "window_context/Application.h"

namespace Rapture {

static VkDeviceSize s_roundUp(VkDeviceSize value, VkDeviceSize multiple)
{
    if (multiple <= 1) return value;
    return ((value + multiple - 1) / multiple) * multiple;
}

VirtualStorageBuffer::VirtualStorageBuffer(VkDeviceSize capacityBytes, VkDeviceSize blockSize,
                                           DescriptorSetBindingLocation binding)
    : m_capacity(capacityBytes), m_blockSize(blockSize == 0 ? 1 : blockSize), m_binding(binding)
{
    auto &vc = Application::getInstance().getVulkanContext();
    m_buffer = std::make_unique<StorageBuffer>(capacityBytes, BufferUsage::DYNAMIC, vc.getVmaAllocator(), nullptr);

    VmaVirtualBlockCreateInfo blockCreateInfo{};
    blockCreateInfo.size = capacityBytes;
    if (vmaCreateVirtualBlock(&blockCreateInfo, &m_block) != VK_SUCCESS) {
        RP_CORE_ERROR("VirtualStorageBuffer failed to create its virtual block of {} bytes", capacityBytes);
        m_block = VK_NULL_HANDLE;
    }

    auto set = Application::getRenderContext().descriptorManager->getDescriptorSet(binding);
    if (set != nullptr) {
        auto ssboBinding = set->getSSBOBinding(binding);
        if (ssboBinding != nullptr) {
            m_descriptorIndex = ssboBinding->add(*m_buffer);
        }
    }

    if (m_descriptorIndex == UINT32_MAX) {
        RP_CORE_ERROR("VirtualStorageBuffer failed to register at binding {}", static_cast<uint32_t>(binding));
    }
}

VirtualStorageBuffer::~VirtualStorageBuffer()
{
    if (m_descriptorIndex != UINT32_MAX) {
        auto set = Application::getRenderContext().descriptorManager->getDescriptorSet(m_binding);
        if (set != nullptr) {
            auto ssboBinding = set->getSSBOBinding(m_binding);
            if (ssboBinding != nullptr) {
                ssboBinding->free(m_descriptorIndex);
            }
        }
    }
    if (m_block != VK_NULL_HANDLE) {
        vmaDestroyVirtualBlock(m_block);
    }
}

bool VirtualStorageBuffer::allocate(VkDeviceSize sizeBytes, VkDeviceSize &outOffsetBytes)
{
    if (m_block == VK_NULL_HANDLE || sizeBytes == 0) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    VmaVirtualAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.size = s_roundUp(sizeBytes, m_blockSize);
    allocCreateInfo.alignment = m_blockSize;

    VmaVirtualAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkResult result = vmaVirtualAllocate(m_block, &allocCreateInfo, &allocation, &offset);

    if (result == VK_SUCCESS && (offset % m_blockSize) == 0) {
        m_allocations[offset] = allocation;
        outOffsetBytes = offset;
        return true;
    }

    // VMA gave a misaligned offset, over-allocate and round the offset up by hand
    if (result == VK_SUCCESS) {
        vmaVirtualFree(m_block, allocation);
    }
    allocCreateInfo.size = s_roundUp(sizeBytes, m_blockSize) + (m_blockSize - 1);
    allocCreateInfo.alignment = 1;
    result = vmaVirtualAllocate(m_block, &allocCreateInfo, &allocation, &offset);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("VirtualStorageBuffer is full, could not reserve {} bytes", sizeBytes);
        return false;
    }

    VkDeviceSize remainder = offset % m_blockSize;
    VkDeviceSize aligned = remainder == 0 ? offset : offset + (m_blockSize - remainder);
    m_allocations[aligned] = allocation;
    outOffsetBytes = aligned;
    return true;
}

void VirtualStorageBuffer::free(VkDeviceSize offsetBytes)
{
    if (m_block == VK_NULL_HANDLE) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(offsetBytes);
    if (it == m_allocations.end()) {
        RP_CORE_ERROR("VirtualStorageBuffer freeing an unknown byte offset {}, double free or bad offset", offsetBytes);
        return;
    }
    vmaVirtualFree(m_block, it->second);
    m_allocations.erase(it);
}

void VirtualStorageBuffer::write(VkDeviceSize offsetBytes, const void *data, VkDeviceSize sizeBytes)
{
    if (m_buffer == nullptr || data == nullptr) return;
    if (offsetBytes + sizeBytes > m_capacity) {
        RP_CORE_ERROR("VirtualStorageBuffer write of {} bytes at offset {} exceeds capacity {}", sizeBytes, offsetBytes,
                      m_capacity);
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer->addData(const_cast<void *>(data), sizeBytes, offsetBytes);
}

} // namespace Rapture

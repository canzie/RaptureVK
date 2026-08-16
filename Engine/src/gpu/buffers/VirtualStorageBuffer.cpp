#include "VirtualStorageBuffer.h"

#include "app/Application.h"
#include "core/utils/Log.h"
#include "gpu/buffers/StorageBuffer.h"
#include "gpu/descriptors/DescriptorBinding.h"
#include "gpu/descriptors/DescriptorManager.h"

namespace Rapture {

static VkDeviceSize s_roundUp(VkDeviceSize value, VkDeviceSize multiple)
{
    if (multiple <= 1) return value;
    return ((value + multiple - 1) / multiple) * multiple;
}

VirtualStorageBuffer::Allocation::Allocation(Allocation &&other) noexcept
{
    *this = std::move(other);
}

VirtualStorageBuffer::Allocation &VirtualStorageBuffer::Allocation::operator=(Allocation &&other) noexcept
{
    if (this != &other) {
        m_handle = other.m_handle;
        m_offsetBytes = other.m_offsetBytes;
        m_sizeBytes = other.m_sizeBytes;
        other.m_handle = VK_NULL_HANDLE;
    }
    return *this;
}

VirtualStorageBuffer::VirtualStorageBuffer(VkDeviceSize capacityBytes, VkDeviceSize blockSize, DescriptorSetBindingLocation binding)
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

VirtualStorageBuffer::Allocation VirtualStorageBuffer::allocate(VkDeviceSize sizeBytes)
{
    if (m_block == VK_NULL_HANDLE || sizeBytes == 0) {
        return {};
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    VmaVirtualAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.size = s_roundUp(sizeBytes, m_blockSize);
    allocCreateInfo.alignment = m_blockSize;

    VmaVirtualAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkResult result = vmaVirtualAllocate(m_block, &allocCreateInfo, &allocation, &offset);

    if (result != VK_SUCCESS || (offset % m_blockSize) != 0) {
        // VMA gave a misaligned offset, over-allocate and round the offset up by hand
        if (result == VK_SUCCESS) {
            vmaVirtualFree(m_block, allocation);
        }
        allocCreateInfo.size = s_roundUp(sizeBytes, m_blockSize) + (m_blockSize - 1);
        allocCreateInfo.alignment = 1;
        result = vmaVirtualAllocate(m_block, &allocCreateInfo, &allocation, &offset);
        if (result != VK_SUCCESS) {
            RP_CORE_ERROR("VirtualStorageBuffer is full, could not reserve {} bytes", sizeBytes);
            return {};
        }

        VkDeviceSize remainder = offset % m_blockSize;
        offset = remainder == 0 ? offset : offset + (m_blockSize - remainder);
    }

    Allocation reserved;
    reserved.m_handle = allocation;
    reserved.m_offsetBytes = offset;
    reserved.m_sizeBytes = sizeBytes;
    return reserved;
}

void VirtualStorageBuffer::free(Allocation &allocation)
{
    if (m_block == VK_NULL_HANDLE || !allocation.isValid()) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    vmaVirtualFree(m_block, allocation.m_handle);
    allocation.m_handle = VK_NULL_HANDLE;
}

void VirtualStorageBuffer::write(const Allocation &allocation, std::span<const std::byte> data, VkDeviceSize offsetBytes)
{
    if (m_buffer == nullptr || !allocation.isValid() || data.empty()) {
        return;
    }
    if (offsetBytes + data.size() > allocation.m_sizeBytes) {
        RP_CORE_ERROR("VirtualStorageBuffer write of {} bytes at offset {} overruns a range of {} bytes", data.size(), offsetBytes,
                      allocation.m_sizeBytes);
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer->addData(const_cast<void *>(static_cast<const void *>(data.data())), data.size(),
                      allocation.m_offsetBytes + offsetBytes);
}

} // namespace Rapture

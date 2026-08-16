#include "FreeListStorageBuffer.h"

#include "app/Application.h"
#include "core/utils/Log.h"
#include "gpu/buffers/StorageBuffer.h"
#include "gpu/descriptors/DescriptorBinding.h"
#include "gpu/descriptors/DescriptorManager.h"

namespace Rapture {

FreeListStorageBuffer::FreeListStorageBuffer(uint32_t elementSize, uint32_t capacity, DescriptorSetBindingLocation binding)
    : m_elementSize(elementSize), m_capacity(capacity), m_binding(binding)
{
    auto &vc = Application::getInstance().getVulkanContext();
    m_buffer = std::make_unique<StorageBuffer>(static_cast<VkDeviceSize>(elementSize) * capacity, BufferUsage::DYNAMIC,
                                               vc.getVmaAllocator(), nullptr);

    auto set = Application::getRenderContext().descriptorManager->getDescriptorSet(binding);
    if (set != nullptr) {
        auto ssboBinding = set->getSSBOBinding(binding);
        if (ssboBinding != nullptr) {
            m_descriptorIndex = ssboBinding->add(*m_buffer);
        }
    }

    if (m_descriptorIndex == UINT32_MAX) {
        RP_CORE_ERROR("FreeListStorageBuffer failed to register at binding {}", static_cast<uint32_t>(binding));
    }
}

FreeListStorageBuffer::~FreeListStorageBuffer()
{
    if (m_descriptorIndex == UINT32_MAX) {
        return;
    }
    auto set = Application::getRenderContext().descriptorManager->getDescriptorSet(m_binding);
    if (set != nullptr) {
        auto ssboBinding = set->getSSBOBinding(m_binding);
        if (ssboBinding != nullptr) {
            ssboBinding->free(m_descriptorIndex);
        }
    }
}

uint32_t FreeListStorageBuffer::allocate()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_freeSlots.empty()) {
        uint32_t slot = m_freeSlots.back();
        m_freeSlots.pop_back();
        return slot;
    }
    if (m_nextSlot >= m_capacity) {
        RP_CORE_ERROR("FreeListStorageBuffer full (capacity {})", m_capacity);
        return UINT32_MAX;
    }
    return m_nextSlot++;
}

void FreeListStorageBuffer::free(uint32_t slot)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (slot < m_capacity) {
        m_freeSlots.push_back(slot);
    }
}

void FreeListStorageBuffer::write(uint32_t slot, const void *data)
{
    if (slot >= m_capacity || m_buffer == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer->addData(const_cast<void *>(data), m_elementSize, static_cast<VkDeviceSize>(slot) * m_elementSize);
}

} // namespace Rapture

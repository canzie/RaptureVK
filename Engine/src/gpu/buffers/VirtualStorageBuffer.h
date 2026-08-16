#ifndef RAPTURE__VIRTUAL_STORAGE_BUFFER_H
#define RAPTURE__VIRTUAL_STORAGE_BUFFER_H

#include "gpu/descriptors/DescriptorSet.h"

#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>

namespace Rapture {

class StorageBuffer;

/**
 * @brief A host-visible SSBO sub-allocated into variable-size byte ranges by a VMA virtual block
 *
 * Every allocation is aligned and rounded up to a fixed block size set at construction, so the
 * arena stays block-granular and its fragmentation is bounded.
 */
class VirtualStorageBuffer {
  public:
    /**
     * @brief A range reserved from the arena, handed back to write or free it
     */
    struct Allocation {
        Allocation() = default;
        Allocation(const Allocation &) = delete;
        Allocation &operator=(const Allocation &) = delete;
        Allocation(Allocation &&other) noexcept;
        Allocation &operator=(Allocation &&other) noexcept;

        bool isValid() const { return m_handle != VK_NULL_HANDLE; }
        VkDeviceSize getOffsetBytes() const { return m_offsetBytes; }
        VkDeviceSize getSizeBytes() const { return m_sizeBytes; }

      private:
        friend class VirtualStorageBuffer;

        VmaVirtualAllocation m_handle = VK_NULL_HANDLE;
        VkDeviceSize m_offsetBytes = 0;
        VkDeviceSize m_sizeBytes = 0;
    };

    /**
     * @brief Create the arena buffer and register it at a descriptor binding
     * @param capacityBytes Total arena size in bytes
     * @param blockSize Alignment and size granularity every allocation is rounded up to, a power of two
     * @param binding Descriptor set binding the buffer is bound at
     */
    VirtualStorageBuffer(VkDeviceSize capacityBytes, VkDeviceSize blockSize, DescriptorSetBindingLocation binding);
    ~VirtualStorageBuffer();

    VirtualStorageBuffer(const VirtualStorageBuffer &) = delete;
    VirtualStorageBuffer &operator=(const VirtualStorageBuffer &) = delete;

    /**
     * @brief Reserve a byte range from the arena, sized up to the block granularity
     * @param sizeBytes Number of bytes to reserve, rounded up to the block size
     * @return The range, invalid if the arena is full
     */
    Allocation allocate(VkDeviceSize sizeBytes);

    /**
     * @brief Release a previously reserved range, leaving it invalid
     * @param allocation The range returned by allocate
     */
    void free(Allocation &allocation);

    /**
     * @brief Upload bytes into a reserved range
     * @param allocation The range to write into
     * @param data The bytes to upload
     * @param offsetBytes Byte offset within the range to start writing at
     */
    void write(const Allocation &allocation, std::span<const std::byte> data, VkDeviceSize offsetBytes = 0);

    VkDeviceSize getCapacity() const { return m_capacity; }

  private:
    std::unique_ptr<StorageBuffer> m_buffer;
    VmaVirtualBlock m_block = VK_NULL_HANDLE;
    uint32_t m_descriptorIndex = UINT32_MAX;
    VkDeviceSize m_capacity;
    VkDeviceSize m_blockSize;
    DescriptorSetBindingLocation m_binding;
    std::mutex m_mutex;
};

} // namespace Rapture

#endif // RAPTURE__VIRTUAL_STORAGE_BUFFER_H

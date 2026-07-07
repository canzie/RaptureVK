#ifndef RAPTURE__VIRTUAL_STORAGE_BUFFER_H
#define RAPTURE__VIRTUAL_STORAGE_BUFFER_H

#include "buffers/descriptors/DescriptorSet.h"

#include <vk_mem_alloc.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace Rapture {

class StorageBuffer;

/**
 * @brief A host-visible SSBO sub-allocated into variable-size byte ranges by a VMA virtual block
 *
 * Every allocation is aligned and rounded up to a fixed block size set at construction, so the
 * arena stays block-granular and its fragmentation is bounded. Callers address a range by its byte
 * offset, both to write it and to free it; the underlying VMA handles stay internal.
 *
 * TODO: the raw byte offset is too fragile as a caller-held handle (no validation, blocks
 * relocation/defrag); replace it with an opaque slot handle later.
 */
class VirtualStorageBuffer {
  public:
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
     * @param outOffsetBytes Filled with the block-aligned byte offset of the range
     * @return True on success, false if the arena is full
     */
    bool allocate(VkDeviceSize sizeBytes, VkDeviceSize &outOffsetBytes);

    /**
     * @brief Release a previously reserved range
     * @param offsetBytes The byte offset returned by allocate
     */
    void free(VkDeviceSize offsetBytes);

    /**
     * @brief Upload bytes into the arena at a byte offset
     * @param offsetBytes Byte offset to write at, usually a range's outOffsetBytes
     * @param data Pointer to the bytes to upload
     * @param sizeBytes Number of bytes to write
     */
    void write(VkDeviceSize offsetBytes, const void *data, VkDeviceSize sizeBytes);

    VkDeviceSize getCapacity() const { return m_capacity; }

  private:
    std::unique_ptr<StorageBuffer> m_buffer;
    VmaVirtualBlock m_block = VK_NULL_HANDLE;
    std::unordered_map<VkDeviceSize, VmaVirtualAllocation> m_allocations; // byte offset to its VMA handle
    uint32_t m_descriptorIndex = UINT32_MAX;
    VkDeviceSize m_capacity;
    VkDeviceSize m_blockSize;
    DescriptorSetBindingLocation m_binding;
    std::mutex m_mutex;
};

} // namespace Rapture

#endif // RAPTURE__VIRTUAL_STORAGE_BUFFER_H

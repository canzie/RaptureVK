#ifndef RAPTURE__FREE_LIST_STORAGE_BUFFER_H
#define RAPTURE__FREE_LIST_STORAGE_BUFFER_H

#include "buffers/descriptors/DescriptorSet.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Rapture {

class StorageBuffer;

/**
 * @brief Fixed-capacity SSBO of uniform-size elements with free-list slot allocation
 *
 */
class FreeListStorageBuffer {
  public:
    FreeListStorageBuffer(uint32_t elementSize, uint32_t capacity, DescriptorSetBindingLocation binding);
    ~FreeListStorageBuffer();

    FreeListStorageBuffer(const FreeListStorageBuffer &) = delete;
    FreeListStorageBuffer &operator=(const FreeListStorageBuffer &) = delete;

    /**
     * @brief Reserve a slot
     * @return Slot index, or UINT32_MAX if the arena is full
     */
    uint32_t allocate();

    /**
     * @brief Release a slot back to the free list
     * @param slot Slot index to release
     */
    void free(uint32_t slot);

    /**
     * @brief Write elementSize bytes into the given slot
     * @param slot Slot index to write
     * @param data Pointer to elementSize bytes to upload
     */
    void write(uint32_t slot, const void *data);

    StorageBuffer *getBuffer() const { return m_buffer.get(); }
    uint32_t getCapacity() const { return m_capacity; }

  private:
    std::unique_ptr<StorageBuffer> m_buffer;
    std::vector<uint32_t> m_freeSlots;
    uint32_t m_nextSlot = 0;
    uint32_t m_elementSize;
    uint32_t m_capacity;
    uint32_t m_descriptorIndex = UINT32_MAX;
    DescriptorSetBindingLocation m_binding;
    std::mutex m_mutex;
};

} // namespace Rapture

#endif // RAPTURE__FREE_LIST_STORAGE_BUFFER_H

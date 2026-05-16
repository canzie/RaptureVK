#ifndef RAPTURE__RENDERPARTITION_H
#define RAPTURE__RENDERPARTITION_H

#include "buffers/descriptors/DescriptorSet.h"
#include "components/ComponentsCommon.h"
#include "scenes/entities/EntityCommon.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Rapture {

struct RenderContext;
class StorageBuffer;

/**
 * @brief Cache-friendly bit array for tracking which SSBO slots need re-upload
 */
class DirtyBitfield {
  public:
    /**
     * @brief Resize to accommodate the given number of slots
     * @param slotCount Total number of slots
     */
    void resize(uint32_t slotCount);

    /**
     * @brief Mark a slot as dirty
     * @param slot Dense index to mark
     */
    void set(uint32_t slot);

    /**
     * @brief Clear all dirty bits
     */
    void clearAll();

    /**
     * @brief Check if any slot is dirty
     * @return True if at least one bit is set
     */
    bool hasAnyDirty() const;

    /**
     * @brief Invoke a callback for each dirty slot index
     * @param fn Callable with signature void(uint32_t slotIndex)
     */
    template <typename Fn> void forEachDirty(Fn &&fn) const
    {
        for (uint32_t wordIdx = 0; wordIdx < static_cast<uint32_t>(m_words.size()); wordIdx++) {
            uint64_t word = m_words[wordIdx];
            while (word != 0) {
                uint32_t bit = static_cast<uint32_t>(__builtin_ctzll(word));
                uint32_t slot = wordIdx * 64 + bit;
                if (slot < m_slotCount) {
                    fn(slot);
                }
                word &= word - 1;
            }
        }
    }

  private:
    std::vector<uint64_t> m_words;
    uint32_t m_slotCount = 0;
    bool m_anyDirty = false;
};

/**
 * @brief Dense slot map with per-frame dirty tracking for GPU data
 *
 * Packed array of T (SSBO contents) with O(1) add/remove via swap-and-pop.
 * Sparse array maps EntityID to dense index. One dirty bitfield per frame in flight.
 */
template <typename T> class RenderPartition {
  public:
    using SwapCallback = std::function<void(EntityID entityId, uint32_t newDenseIndex)>;

    /**
     * @brief Initialize dirty bitfields and swap callback
     * @param frameCount Number of frames in flight
     * @param onSwap Called with (entityId, newDenseIndex) when swap-and-pop relocates an entity
     */
    void init(uint32_t frameCount, SwapCallback onSwap = nullptr);

    /**
     * @brief Allocate a new slot for an entity
     * @param entityId Entity to associate with this slot
     * @return Dense index of the new slot
     */
    uint32_t allocateSlot(EntityID entityId);

    /**
     * @brief Free a slot via swap-and-pop
     * @param denseIndex Dense index to free
     */
    void freeSlot(uint32_t denseIndex);

    /**
     * @brief Access slot data by dense index
     * @param denseIndex Index into the packed array
     * @return Reference to the slot data
     */
    T &getSlotData(uint32_t denseIndex);
    const T &getSlotData(uint32_t denseIndex) const;

    /**
     * @brief Get the entity ID stored at a dense index
     * @param denseIndex Index into the packed array
     * @return The entity ID at that slot
     */
    EntityID getEntityId(uint32_t denseIndex) const;

    /**
     * @brief Get the number of active slots
     * @return Count of allocated slots
     */
    uint32_t getCount() const;

    /**
     * @brief Raw pointer to the packed data array
     * @return Pointer to first element, or nullptr if empty
     */
    T *getData();
    const T *getData() const;

    /**
     * @brief Mark a slot dirty in all per-frame bitfields
     * @param denseIndex Slot to mark
     */
    void markDirtyAllFrames(uint32_t denseIndex);

    /**
     * @brief Check if any slot is dirty for a given frame
     * @param frameIndex Frame to check
     * @return True if at least one slot needs re-upload
     */
    bool hasDirty(uint32_t frameIndex) const;

    /**
     * @brief Invoke a callback for each dirty slot in a frame's bitfield
     * @param frameIndex Frame to query
     * @param fn Callback invoked with each dirty slot index
     */
    void forEachDirty(uint32_t frameIndex, const std::function<void(uint32_t)> &fn) const;

    /**
     * @brief Clear the dirty bitfield for a frame after upload
     * @param frameIndex Frame to clear
     */
    void clearDirty(uint32_t frameIndex);

    /**
     * @brief Get the last observed generation for a slot
     * @param denseIndex Slot to query
     * @return Generation counter from the last update
     */
    generation_t getLastSeenGeneration(uint32_t denseIndex) const;

    /**
     * @brief Store the current generation for a slot
     * @param denseIndex Slot to update
     * @param gen Current generation value
     */
    void setLastSeenGeneration(uint32_t denseIndex, generation_t gen);

    /**
     * @brief Fire swap callback for every entry so components can recompute their renderDataSlot
     */
    void notifyAllSwaps();

    /**
     * @brief Mark every slot dirty in all per-frame bitfields
     */
    void markAllDirtyAllFrames();

  private:
    std::vector<T> m_data;
    std::vector<EntityID> m_denseToEntityId;
    std::vector<DirtyBitfield> m_dirtyBitfields;
    std::vector<generation_t> m_lastSeenGenerations;
    SwapCallback m_onSwap;
    uint32_t m_frameCount = 0;
};

/**
 * @brief Bundles static + dynamic partitions with per-frame SSBOs for one data type
 *
 * Owns two RenderPartitions and a set of SSBOs (one per frame in flight).
 * SSBO layout: [static region: staticCapacity][dynamic region: dynamicCapacity]
 * Each region grows independently. Dynamic global slots only shift on static
 * capacity resize (rare), not on every static add/remove.
 *
 * If per-mobility SSBOs are ever needed (e.g. GPU_ONLY for statics), the
 * renderDataSlot can encode both SSBO index and slot via bit packing
 * (upper bits = SSBO index, lower bits = slot within that SSBO)
 */
template <typename T> class GPUDataStore {
  public:
    GPUDataStore();
    ~GPUDataStore();

    /**
     * @brief Initialize partitions and allocate per-frame SSBOs
     * @param frameCount Number of frames in flight
     * @param renderContext Vulkan context for buffer allocation
     * @param bindingLocation Descriptor set binding for SSBO registration
     */
    void init(uint32_t frameCount, RenderContext *renderContext, DescriptorSetBindingLocation bindingLocation);

    /**
     * @brief Get the total number of active slots across both partitions
     * @return Static count + dynamic count
     */
    uint32_t getTotalCount() const;

    /**
     * @brief Upload partition data to the SSBO for the given frame
     * @param frameIndex Current frame in flight index
     */
    void upload(uint32_t frameIndex);

    /**
     * @brief Get the SSBO for a given frame
     * @param frameIndex Frame in flight index
     * @return Pointer to the StorageBuffer, or nullptr if not allocated
     */
    StorageBuffer *getSSBO(uint32_t frameIndex) const;

    /**
     * @brief Get the descriptor index for a frame's SSBO
     * @param frameIndex Frame in flight index
     * @return Descriptor index in the binding, or UINT32_MAX if not registered
     */
    uint32_t getDescriptorIndex(uint32_t frameIndex) const;

    /**
     * @brief Get the partition for a given mobility type
     * @param mobility MOBILITY_STATIC, MOBILITY_DYNAMIC, etc
     * @return Reference to the partition
     */
    RenderPartition<T> &getPartition(Mobility mobility) { return m_partitions[mobility]; }
    const RenderPartition<T> &getPartition(Mobility mobility) const { return m_partitions[mobility]; }

    /**
     * @brief Convert a partition-local dense index to the global SSBO index
     * @param mobility Partition the slot belongs to
     * @param localSlot Dense index within that partition
     * @return Index into the unified SSBO
     */
    uint32_t getGlobalSlot(Mobility mobility, uint32_t localSlot) const;

    /**
     * @brief Convert a global SSBO index back to a partition-local dense index
     * @param mobility Partition the slot belongs to
     * @param globalSlot Index into the unified SSBO
     * @return Dense index within that partition
     */
    uint32_t getLocalSlot(Mobility mobility, uint32_t globalSlot) const;

  private:
    void ensureCapacity(uint32_t requiredStaticCount, uint32_t requiredDynamicCount);
    void registerSSBOs();
    void unregisterSSBOs();

    std::array<RenderPartition<T>, MOBILITY_COUNT> m_partitions;
    std::vector<std::unique_ptr<StorageBuffer>> m_ssbos;
    std::vector<uint32_t> m_descriptorIndices;
    uint32_t m_staticCapacity = 0;
    uint32_t m_dynamicCapacity = 0;
    uint32_t m_frameCount = 0;
    RenderContext *m_renderContext = nullptr;
    DescriptorSetBindingLocation m_bindingLocation = DescriptorSetBindingLocation::NONE;
};

} // namespace Rapture

#endif // RAPTURE__RENDERPARTITION_H

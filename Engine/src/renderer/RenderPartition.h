#ifndef RAPTURE__RENDERPARTITION_H
#define RAPTURE__RENDERPARTITION_H

#include "components/ComponentsCommon.h"
#include "scenes/entities/EntityCommon.h"

#include <array>
#include <cstdint>
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

  private:
    std::vector<uint64_t> m_words;
    uint32_t m_slotCount = 0;
};

/**
 * @brief Dense slot map with per-frame dirty tracking for GPU data
 *
 * Packed array of T (SSBO contents) with O(1) add/remove via swap-and-pop.
 * Sparse array maps EntityID to dense index. One dirty bitfield per frame in flight.
 */
template <typename T> class RenderPartition {
  public:
    /**
     * @brief Initialize dirty bitfields for N frames in flight
     * @param frameCount Number of frames in flight
     */
    void init(uint32_t frameCount);

    /**
     * @brief Allocate a new slot for an entity
     * @param entityId Entity to allocate for
     * @return Dense index of the new slot
     */
    uint32_t allocateSlot(EntityID entityId);

    /**
     * @brief Free an entity's slot via swap-and-pop
     * @param entityId Entity to remove
     */
    void freeSlot(EntityID entityId);

    /**
     * @brief Access slot data by dense index
     * @param denseIndex Index into the packed array
     * @return Reference to the slot data
     */
    T &getSlotData(uint32_t denseIndex);
    const T &getSlotData(uint32_t denseIndex) const;

    /**
     * @brief Look up the dense index for an entity
     * @param entityId Entity to look up
     * @return Dense index, or UINT32_MAX if not found
     */
    uint32_t getSlotIndex(EntityID entityId) const;

    /**
     * @brief Check if an entity has a slot
     * @param entityId Entity to check
     * @return True if a slot is allocated
     */
    bool hasSlot(EntityID entityId) const;

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

  private:
    std::vector<T> m_data;
    std::vector<EntityID> m_denseToEntityId;
    std::vector<uint32_t> m_sparse;
    std::vector<DirtyBitfield> m_dirtyBitfields;
    std::vector<generation_t> m_lastSeenGenerations;
    uint32_t m_frameCount = 0;
};

/**
 * @brief Bundles static + dynamic partitions with per-frame SSBOs for one data type
 *
 * Owns two RenderPartitions and a set of SSBOs (one per frame in flight).
 * Static data comes first in the unified SSBO, then dynamic.
 */
template<typename T>
class GPUDataStore {
  public:
    GPUDataStore();
    ~GPUDataStore();

    /**
     * @brief Initialize partitions and allocate per-frame SSBOs
     * @param frameCount Number of frames in flight
     * @param renderContext Vulkan context for buffer allocation
     */
    void init(uint32_t frameCount, RenderContext* renderContext);

    /**
     * @brief Allocate a slot in either the static or dynamic partition
     * @param entityId Entity to allocate for
     * @param mobility MOBILITY_STATIC or MOBILITY_DYNAMIC
     * @return Dense index within the chosen partition
     */
    uint32_t allocateSlot(EntityID entityId, Mobility mobility);

    /**
     * @brief Free an entity's slot from whichever partition holds it
     * @param entityId Entity to remove
     */
    void freeSlot(EntityID entityId);

    /**
     * @brief Get the global SSBO index for an entity (statics first, then dynamics)
     * @param entityId Entity to look up
     * @return Global index, or UINT32_MAX if not found
     */
    uint32_t getSlotIndex(EntityID entityId) const;

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
    StorageBuffer* getSSBO(uint32_t frameIndex) const;

    RenderPartition<T>& getStaticPartition() { return m_partitions[MOBILITY_STATIC]; }
    RenderPartition<T>& getDynamicPartition() { return m_partitions[MOBILITY_DYNAMIC]; }
    const RenderPartition<T>& getStaticPartition() const { return m_partitions[MOBILITY_STATIC]; }
    const RenderPartition<T>& getDynamicPartition() const { return m_partitions[MOBILITY_DYNAMIC]; }

  private:
    void ensureCapacity(uint32_t requiredCount);

    std::array<RenderPartition<T>, MOBILITY_COUNT> m_partitions;
    std::vector<std::unique_ptr<StorageBuffer>> m_ssbos;
    uint32_t m_ssboCapacity = 0;
    uint32_t m_frameCount = 0;
    RenderContext* m_renderContext = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__RENDERPARTITION_H

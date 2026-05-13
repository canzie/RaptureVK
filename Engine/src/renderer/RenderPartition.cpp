#include "RenderPartition.h"
#include "GPUDataStructs.h"

#include "buffers/StorageBuffer.h"
#include "window_context/vulkan_context/VulkanContext.h"

#include <cstring>

namespace Rapture {

void DirtyBitfield::resize(uint32_t slotCount)
{
    uint32_t wordCount = (slotCount + 63) / 64;
    m_words.resize(wordCount, 0);
    m_slotCount = slotCount;
}

void DirtyBitfield::set(uint32_t slot)
{
    m_words[slot / 64] |= (1ULL << (slot % 64));
}

void DirtyBitfield::clearAll()
{
    std::memset(m_words.data(), 0, m_words.size() * sizeof(uint64_t));
}

bool DirtyBitfield::hasAnyDirty() const
{
    for (auto word : m_words) {
        if (word != 0) return true;
    }
    return false;
}

template<typename T>
void RenderPartition<T>::init(uint32_t frameCount)
{
    m_frameCount = frameCount;
    m_dirtyBitfields.resize(frameCount);
}

template<typename T>
uint32_t RenderPartition<T>::allocateSlot(EntityID entityId)
{
    if (entityId >= m_sparse.size()) {
        m_sparse.resize(entityId + 1, UINT32_MAX);
    }

    uint32_t denseIndex = static_cast<uint32_t>(m_data.size());
    m_data.emplace_back();
    m_denseToEntityId.push_back(entityId);
    m_lastSeenGenerations.push_back(0);
    m_sparse[entityId] = denseIndex;

    for (auto& bf : m_dirtyBitfields) {
        bf.resize(denseIndex + 1);
        bf.set(denseIndex);
    }

    return denseIndex;
}

template<typename T>
void RenderPartition<T>::freeSlot(EntityID entityId)
{
    if (entityId >= m_sparse.size() || m_sparse[entityId] == UINT32_MAX) {
        return;
    }

    uint32_t denseIndex = m_sparse[entityId];
    uint32_t lastIndex = static_cast<uint32_t>(m_data.size()) - 1;

    if (denseIndex != lastIndex) {
        m_data[denseIndex] = m_data[lastIndex];
        m_denseToEntityId[denseIndex] = m_denseToEntityId[lastIndex];
        m_lastSeenGenerations[denseIndex] = m_lastSeenGenerations[lastIndex];

        m_sparse[m_denseToEntityId[denseIndex]] = denseIndex;

        for (auto& bf : m_dirtyBitfields) {
            bf.set(denseIndex);
        }
    }

    m_data.pop_back();
    m_denseToEntityId.pop_back();
    m_lastSeenGenerations.pop_back();
    m_sparse[entityId] = UINT32_MAX;

    for (auto& bf : m_dirtyBitfields) {
        bf.resize(static_cast<uint32_t>(m_data.size()));
    }
}

template<typename T>
T& RenderPartition<T>::getSlotData(uint32_t denseIndex)
{
    return m_data[denseIndex];
}

template<typename T>
const T& RenderPartition<T>::getSlotData(uint32_t denseIndex) const
{
    return m_data[denseIndex];
}

template<typename T>
uint32_t RenderPartition<T>::getSlotIndex(EntityID entityId) const
{
    if (entityId >= m_sparse.size()) return UINT32_MAX;
    return m_sparse[entityId];
}

template<typename T>
bool RenderPartition<T>::hasSlot(EntityID entityId) const
{
    return getSlotIndex(entityId) != UINT32_MAX;
}

template<typename T>
EntityID RenderPartition<T>::getEntityId(uint32_t denseIndex) const
{
    return m_denseToEntityId[denseIndex];
}

template<typename T>
uint32_t RenderPartition<T>::getCount() const
{
    return static_cast<uint32_t>(m_data.size());
}

template<typename T>
T* RenderPartition<T>::getData()
{
    return m_data.data();
}

template<typename T>
const T* RenderPartition<T>::getData() const
{
    return m_data.data();
}

template<typename T>
void RenderPartition<T>::markDirtyAllFrames(uint32_t denseIndex)
{
    for (auto& bf : m_dirtyBitfields) {
        bf.set(denseIndex);
    }
}

template<typename T>
bool RenderPartition<T>::hasDirty(uint32_t frameIndex) const
{
    return m_dirtyBitfields[frameIndex].hasAnyDirty();
}

template<typename T>
void RenderPartition<T>::clearDirty(uint32_t frameIndex)
{
    m_dirtyBitfields[frameIndex].clearAll();
}

template<typename T>
generation_t RenderPartition<T>::getLastSeenGeneration(uint32_t denseIndex) const
{
    return m_lastSeenGenerations[denseIndex];
}

template<typename T>
void RenderPartition<T>::setLastSeenGeneration(uint32_t denseIndex, generation_t gen)
{
    m_lastSeenGenerations[denseIndex] = gen;
}

template<typename T>
GPUDataStore<T>::GPUDataStore() = default;

template<typename T>
GPUDataStore<T>::~GPUDataStore() = default;

template<typename T>
void GPUDataStore<T>::init(uint32_t frameCount, RenderContext* renderContext)
{
    m_frameCount = frameCount;
    m_renderContext = renderContext;
    m_ssbos.resize(frameCount);

    for (auto& partition : m_partitions) {
        partition.init(frameCount);
    }
}

template<typename T>
uint32_t GPUDataStore<T>::allocateSlot(EntityID entityId, Mobility mobility)
{
    return m_partitions[mobility].allocateSlot(entityId);
}

template<typename T>
void GPUDataStore<T>::freeSlot(EntityID entityId)
{
    for (auto& partition : m_partitions) {
        if (partition.hasSlot(entityId)) {
            partition.freeSlot(entityId);
            return;
        }
    }
}

template<typename T>
uint32_t GPUDataStore<T>::getSlotIndex(EntityID entityId) const
{
    uint32_t staticSlot = m_partitions[MOBILITY_STATIC].getSlotIndex(entityId);
    if (staticSlot != UINT32_MAX) return staticSlot;

    uint32_t dynamicSlot = m_partitions[MOBILITY_DYNAMIC].getSlotIndex(entityId);
    if (dynamicSlot != UINT32_MAX) return m_partitions[MOBILITY_STATIC].getCount() + dynamicSlot;

    return UINT32_MAX;
}

template<typename T>
uint32_t GPUDataStore<T>::getTotalCount() const
{
    uint32_t total = 0;
    for (const auto& partition : m_partitions) {
        total += partition.getCount();
    }
    return total;
}

template<typename T>
void GPUDataStore<T>::upload(uint32_t frameIndex)
{
    uint32_t staticCount = m_partitions[MOBILITY_STATIC].getCount();
    uint32_t dynamicCount = m_partitions[MOBILITY_DYNAMIC].getCount();
    uint32_t totalCount = staticCount + dynamicCount;
    if (totalCount == 0) return;

    ensureCapacity(totalCount);

    auto* ssbo = m_ssbos[frameIndex].get();

    if (staticCount > 0 && m_partitions[MOBILITY_STATIC].hasDirty(frameIndex)) {
        ssbo->addData(m_partitions[MOBILITY_STATIC].getData(),
                      static_cast<VkDeviceSize>(staticCount) * sizeof(T), 0);
        m_partitions[MOBILITY_STATIC].clearDirty(frameIndex);
    }

    if (dynamicCount > 0) {
        ssbo->addData(m_partitions[MOBILITY_DYNAMIC].getData(),
                      static_cast<VkDeviceSize>(dynamicCount) * sizeof(T),
                      static_cast<VkDeviceSize>(staticCount) * sizeof(T));
    }
}

template<typename T>
StorageBuffer* GPUDataStore<T>::getSSBO(uint32_t frameIndex) const
{
    if (frameIndex < m_ssbos.size() && m_ssbos[frameIndex]) {
        return m_ssbos[frameIndex].get();
    }
    return nullptr;
}

template<typename T>
void GPUDataStore<T>::ensureCapacity(uint32_t requiredCount)
{
    if (requiredCount <= m_ssboCapacity) return;

    uint32_t newCapacity = std::max(requiredCount, m_ssboCapacity * 2);
    newCapacity = std::max(newCapacity, 64u);

    VmaAllocator allocator = m_renderContext->vulkanContext->getVmaAllocator();
    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(newCapacity) * sizeof(T);

    for (uint32_t i = 0; i < m_frameCount; i++) {
        m_ssbos[i] = std::make_unique<StorageBuffer>(bufferSize, BufferUsage::DYNAMIC, allocator);
    }

    m_ssboCapacity = newCapacity;
}

#define INSTANTIATE_RENDER_PARTITION(T) \
    template class RenderPartition<T>; \
    template class GPUDataStore<T>;

INSTANTIATE_RENDER_PARTITION(MeshGPUData)
INSTANTIATE_RENDER_PARTITION(LightGPUData)
INSTANTIATE_RENDER_PARTITION(CameraGPUData)

#undef INSTANTIATE_RENDER_PARTITION

} // namespace Rapture

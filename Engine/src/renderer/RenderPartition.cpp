#include "RenderPartition.h"
#include "GPUDataStructs.h"

#include "buffers/StorageBuffer.h"
#include "buffers/descriptors/DescriptorManager.h"
#include "window_context/vulkan_context/RenderContext.h"
#include "window_context/vulkan_context/VulkanContext.h"

#include <cstring>

#define SSBO_MIN_CAPACITY  64u
#define SSBO_GROWTH_FACTOR 2u

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
    m_anyDirty = true;
}

void DirtyBitfield::clearAll()
{
    std::memset(m_words.data(), 0, m_words.size() * sizeof(uint64_t));
    m_anyDirty = false;
}

bool DirtyBitfield::hasAnyDirty() const
{
    return m_anyDirty;
}

template <typename T>
void RenderPartition<T>::init(uint32_t frameCount, SwapCallback onSwap)
{
    m_frameCount = frameCount;
    m_dirtyBitfields.resize(frameCount);
    m_onSwap = onSwap;
}

template <typename T>
uint32_t RenderPartition<T>::allocateSlot(EntityID entityId)
{
    uint32_t denseIndex = static_cast<uint32_t>(m_data.size());
    m_data.emplace_back();
    m_denseToEntityId.push_back(entityId);
    m_lastSeenGenerations.push_back(0);

    for (auto &bf : m_dirtyBitfields) {
        bf.resize(denseIndex + 1);
        bf.set(denseIndex);
    }

    return denseIndex;
}

template <typename T>
void RenderPartition<T>::freeSlot(uint32_t denseIndex)
{
    if (denseIndex >= m_data.size()) {
        return;
    }

    uint32_t lastIndex = static_cast<uint32_t>(m_data.size()) - 1;

    if (denseIndex != lastIndex) {
        m_data[denseIndex] = m_data[lastIndex];
        m_denseToEntityId[denseIndex] = m_denseToEntityId[lastIndex];
        m_lastSeenGenerations[denseIndex] = m_lastSeenGenerations[lastIndex];

        if (m_onSwap) {
            m_onSwap(m_denseToEntityId[denseIndex], denseIndex);
        }

        for (auto &bf : m_dirtyBitfields) {
            bf.set(denseIndex);
        }
    }

    m_data.pop_back();
    m_denseToEntityId.pop_back();
    m_lastSeenGenerations.pop_back();

    for (auto &bf : m_dirtyBitfields) {
        bf.resize(static_cast<uint32_t>(m_data.size()));
    }
}

template <typename T>
T &RenderPartition<T>::getSlotData(uint32_t denseIndex)
{
    return m_data[denseIndex];
}

template <typename T>
const T &RenderPartition<T>::getSlotData(uint32_t denseIndex) const
{
    return m_data[denseIndex];
}

template <typename T>
EntityID RenderPartition<T>::getEntityId(uint32_t denseIndex) const
{
    return m_denseToEntityId[denseIndex];
}

template <typename T>
uint32_t RenderPartition<T>::getCount() const
{
    return static_cast<uint32_t>(m_data.size());
}

template <typename T>
T *RenderPartition<T>::getData()
{
    return m_data.data();
}

template <typename T>
const T *RenderPartition<T>::getData() const
{
    return m_data.data();
}

template <typename T>
void RenderPartition<T>::markDirtyAllFrames(uint32_t denseIndex)
{
    for (auto &bf : m_dirtyBitfields) {
        bf.set(denseIndex);
    }
}

template <typename T>
bool RenderPartition<T>::hasDirty(uint32_t frameIndex) const
{
    return m_dirtyBitfields[frameIndex].hasAnyDirty();
}

template <typename T>
void RenderPartition<T>::forEachDirty(uint32_t frameIndex, const std::function<void(uint32_t)> &fn) const
{
    m_dirtyBitfields[frameIndex].forEachDirty(fn);
}

template <typename T>
void RenderPartition<T>::clearDirty(uint32_t frameIndex)
{
    m_dirtyBitfields[frameIndex].clearAll();
}

template <typename T>
generation_t RenderPartition<T>::getLastSeenGeneration(uint32_t denseIndex) const
{
    return m_lastSeenGenerations[denseIndex];
}

template <typename T>
void RenderPartition<T>::setLastSeenGeneration(uint32_t denseIndex, generation_t gen)
{
    m_lastSeenGenerations[denseIndex] = gen;
}

template <typename T>
void RenderPartition<T>::notifyAllSwaps()
{
    if (!m_onSwap) return;
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_data.size()); i++) {
        m_onSwap(m_denseToEntityId[i], i);
    }
}

template <typename T>
void RenderPartition<T>::markAllDirtyAllFrames()
{
    uint32_t count = static_cast<uint32_t>(m_data.size());
    for (auto &bf : m_dirtyBitfields) {
        bf.resize(count);
        for (uint32_t i = 0; i < count; i++) {
            bf.set(i);
        }
    }
}

template <typename T>
GPUDataStore<T>::GPUDataStore() = default;

template <typename T>
GPUDataStore<T>::~GPUDataStore() = default;

template <typename T>
void GPUDataStore<T>::init(uint32_t frameCount, RenderContext *renderContext, DescriptorSetBindingLocation bindingLocation)
{
    m_frameCount = frameCount;
    m_renderContext = renderContext;
    m_bindingLocation = bindingLocation;
    m_ssbos.resize(frameCount);
    m_descriptorIndices.resize(frameCount, UINT32_MAX);

    for (auto &partition : m_partitions) {
        partition.init(frameCount);
    }
}

template <typename T>
uint32_t GPUDataStore<T>::getTotalCount() const
{
    uint32_t total = 0;
    for (const auto &partition : m_partitions) {
        total += partition.getCount();
    }
    return total;
}

template <typename T>
void GPUDataStore<T>::upload(uint32_t frameIndex)
{
    uint32_t staticCount = m_partitions[MOBILITY_STATIC].getCount();
    uint32_t dynamicCount = m_partitions[MOBILITY_DYNAMIC].getCount();
    if (staticCount + dynamicCount == 0) return;

    ensureCapacity(staticCount, dynamicCount);

    auto *ssbo = m_ssbos[frameIndex].get();

    if (staticCount > 0 && m_partitions[MOBILITY_STATIC].hasDirty(frameIndex)) {
        ssbo->addData(m_partitions[MOBILITY_STATIC].getData(), static_cast<VkDeviceSize>(staticCount) * sizeof(T), 0);
        m_partitions[MOBILITY_STATIC].clearDirty(frameIndex);
    }

    if (dynamicCount > 0) {
        ssbo->addData(m_partitions[MOBILITY_DYNAMIC].getData(), static_cast<VkDeviceSize>(dynamicCount) * sizeof(T),
                      static_cast<VkDeviceSize>(m_staticCapacity) * sizeof(T));
    }
}

template <typename T>
StorageBuffer *GPUDataStore<T>::getSSBO(uint32_t frameIndex) const
{
    if (frameIndex < m_ssbos.size() && m_ssbos[frameIndex]) {
        return m_ssbos[frameIndex].get();
    }
    return nullptr;
}

template <typename T>
void GPUDataStore<T>::ensureCapacity(uint32_t requiredStaticCount, uint32_t requiredDynamicCount)
{
    bool needsRealloc = false;
    bool staticCapChanged = false;

    if (requiredStaticCount > m_staticCapacity) {
        m_staticCapacity = std::max(requiredStaticCount, std::max(m_staticCapacity * SSBO_GROWTH_FACTOR, SSBO_MIN_CAPACITY));
        needsRealloc = true;
        staticCapChanged = true;
    }
    if (requiredDynamicCount > m_dynamicCapacity) {
        m_dynamicCapacity = std::max(requiredDynamicCount, std::max(m_dynamicCapacity * SSBO_GROWTH_FACTOR, SSBO_MIN_CAPACITY));
        needsRealloc = true;
    }

    if (!needsRealloc) return;

    unregisterSSBOs();

    VmaAllocator allocator = m_renderContext->vulkanContext->getVmaAllocator();
    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(m_staticCapacity + m_dynamicCapacity) * sizeof(T);

    for (uint32_t i = 0; i < m_frameCount; i++) {
        m_ssbos[i] = std::make_unique<StorageBuffer>(bufferSize, BufferUsage::STREAM, allocator);
    }

    registerSSBOs();

    for (auto &partition : m_partitions) {
        partition.markAllDirtyAllFrames();
    }

    if (staticCapChanged) {
        m_partitions[MOBILITY_DYNAMIC].notifyAllSwaps();
    }
}

template <typename T>
void GPUDataStore<T>::registerSSBOs()
{
    if (m_bindingLocation == DescriptorSetBindingLocation::NONE) {
        return;
    }

    auto set = m_renderContext->descriptorManager->getDescriptorSet(m_bindingLocation);
    if (set == nullptr) {
        return;
    }
    auto binding = set->getSSBOBinding(m_bindingLocation);
    if (binding == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < m_frameCount; i++) {
        if (m_ssbos[i] != nullptr) {
            m_descriptorIndices[i] = binding->add(*m_ssbos[i]);
        }
    }
}

template <typename T>
void GPUDataStore<T>::unregisterSSBOs()
{
    if (m_bindingLocation == DescriptorSetBindingLocation::NONE) {
        return;
    }

    auto set = m_renderContext->descriptorManager->getDescriptorSet(m_bindingLocation);
    if (set == nullptr) {
        return;
    }
    auto binding = set->getSSBOBinding(m_bindingLocation);
    if (binding == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < m_frameCount; i++) {
        if (m_descriptorIndices[i] != UINT32_MAX) {
            binding->free(m_descriptorIndices[i]);
            m_descriptorIndices[i] = UINT32_MAX;
        }
    }
}

template <typename T>
uint32_t GPUDataStore<T>::getDescriptorIndex(uint32_t frameIndex) const
{
    if (frameIndex < m_descriptorIndices.size()) {
        return m_descriptorIndices[frameIndex];
    }
    return UINT32_MAX;
}

template <typename T>
uint32_t GPUDataStore<T>::getGlobalSlot(Mobility mobility, uint32_t localSlot) const
{
    if (mobility == MOBILITY_DYNAMIC) {
        return m_staticCapacity + localSlot;
    }
    return localSlot;
}

template <typename T>
uint32_t GPUDataStore<T>::getLocalSlot(Mobility mobility, uint32_t globalSlot) const
{
    if (mobility == MOBILITY_DYNAMIC) {
        return globalSlot - m_staticCapacity;
    }
    return globalSlot;
}

#define INSTANTIATE_RENDER_PARTITION(T) \
    template class RenderPartition<T>;  \
    template class GPUDataStore<T>;

INSTANTIATE_RENDER_PARTITION(MeshGPUData)
INSTANTIATE_RENDER_PARTITION(LightGPUData)
INSTANTIATE_RENDER_PARTITION(CameraGPUData)
INSTANTIATE_RENDER_PARTITION(ShadowGPUData)

#undef INSTANTIATE_RENDER_PARTITION

} // namespace Rapture

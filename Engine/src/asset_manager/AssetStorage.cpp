#include "AssetStorage.h"

#include "utils/rp_assert.h"

namespace Rapture {

static constexpr uint32_t SLOT_BITS = 24;
static constexpr uint32_t SLOT_MASK = (1u << SLOT_BITS) - 1;

static_assert(ASSET_TYPE_COUNT <= (1u << (32 - SLOT_BITS)), "asset types no longer fit in the packed location");

static uint32_t s_packLocation(AssetType type, uint32_t slot)
{
    return (static_cast<uint32_t>(type) << SLOT_BITS) | slot;
}

static AssetType s_locationType(uint32_t packed)
{
    return static_cast<AssetType>(packed >> SLOT_BITS);
}

static uint32_t s_locationSlot(uint32_t packed)
{
    return packed & SLOT_MASK;
}

AssetSlot *AssetStorage::find(AssetHandle handle)
{
    auto it = m_index.find(handle);
    if (it == m_index.end()) {
        return nullptr;
    }
    return &m_buckets[s_locationType(it->second)][s_locationSlot(it->second)];
}

const AssetSlot *AssetStorage::find(AssetHandle handle) const
{
    auto it = m_index.find(handle);
    if (it == m_index.end()) {
        return nullptr;
    }
    return &m_buckets[s_locationType(it->second)][s_locationSlot(it->second)];
}

AssetSlot &AssetStorage::allocate(AssetHandle handle, AssetType type)
{
    std::vector<AssetSlot> &bucket = m_buckets[type];
    std::vector<uint32_t> &freeSlots = m_freeSlots[type];

    uint32_t slot = 0;
    if (!freeSlots.empty()) {
        slot = freeSlots.back();
        freeSlots.pop_back();
    } else {
        slot = static_cast<uint32_t>(bucket.size());
        RP_ASSERT(slot <= SLOT_MASK, "asset bucket outgrew the packed slot index");
        bucket.emplace_back();
    }

    AssetSlot &entry = bucket[slot];
    entry.handle = handle;
    m_index[handle] = s_packLocation(type, slot);
    return entry;
}

AssetSlot &AssetStorage::insert(AssetHandle handle, std::unique_ptr<AssetMetadata> metadata)
{
    RP_ASSERT(metadata != nullptr, "cannot register an asset without metadata");
    AssetType type = metadata->assetType;

    auto it = m_index.find(handle);
    if (it != m_index.end()) {
        AssetType currentType = s_locationType(it->second);
        AssetSlot &current = m_buckets[currentType][s_locationSlot(it->second)];
        if (currentType == type) {
            current.metadata = std::move(metadata);
            return current;
        }

        // The type decides the bucket, so a retyped asset moves and carries whatever it had loaded with it
        std::unique_ptr<Asset> loaded = std::move(current.asset);
        erase(handle);
        AssetSlot &moved = allocate(handle, type);
        moved.metadata = std::move(metadata);
        moved.asset = std::move(loaded);
        return moved;
    }

    AssetSlot &slot = allocate(handle, type);
    slot.metadata = std::move(metadata);
    return slot;
}

bool AssetStorage::erase(AssetHandle handle)
{
    auto it = m_index.find(handle);
    if (it == m_index.end()) {
        return false;
    }

    AssetType type = s_locationType(it->second);
    uint32_t slot = s_locationSlot(it->second);

    m_buckets[type][slot] = AssetSlot{};
    m_freeSlots[type].push_back(slot);
    m_index.erase(it);
    return true;
}

void AssetStorage::clear()
{
    m_index.clear();
    for (std::vector<AssetSlot> &bucket : m_buckets) {
        bucket.clear();
    }
    for (std::vector<uint32_t> &freeSlots : m_freeSlots) {
        freeSlots.clear();
    }
}

std::vector<AssetHandle> AssetStorage::handlesOfType(AssetType type) const
{
    std::vector<AssetHandle> handles;
    handles.reserve(countOfType(type));
    for (const AssetSlot &slot : ofType(type)) {
        handles.push_back(slot.handle);
    }
    return handles;
}

} // namespace Rapture

#ifndef RAPTURE__ASSET_STORAGE_H
#define RAPTURE__ASSET_STORAGE_H

#include "Asset.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace Rapture {

/**
 * @brief A registered asset, its metadata and its payload once loaded
 */
struct AssetSlot {
    AssetHandle handle = INVALID_ASSET_HANDLE;
    std::unique_ptr<AssetMetadata> metadata;
    std::unique_ptr<Asset> asset;

    bool isOccupied() const { return metadata != nullptr; }
    bool isLoaded() const { return asset != nullptr && asset->isValid(); }
};

/**
 * @brief A range over the live assets of one type, skipping the holes a free list leaves behind
 */
class AssetTypeView {
  public:
    class Iterator {
      public:
        Iterator(const AssetSlot *at, const AssetSlot *end) : m_at(at), m_end(end) { skipHoles(); }

        const AssetSlot &operator*() const { return *m_at; }
        const AssetSlot *operator->() const { return m_at; }
        bool operator==(const Iterator &other) const { return m_at == other.m_at; }

        Iterator &operator++()
        {
            ++m_at;
            skipHoles();
            return *this;
        }

      private:
        void skipHoles()
        {
            while (m_at != m_end && !m_at->isOccupied()) {
                ++m_at;
            }
        }

        const AssetSlot *m_at = nullptr;
        const AssetSlot *m_end = nullptr;
    };

  public:
    AssetTypeView(std::span<const AssetSlot> slots, size_t count) : m_slots(slots), m_count(count) {}

    Iterator begin() const { return Iterator(m_slots.data(), m_slots.data() + m_slots.size()); }
    Iterator end() const { return Iterator(m_slots.data() + m_slots.size(), m_slots.data() + m_slots.size()); }

    size_t size() const { return m_count; }
    bool empty() const { return m_count == 0; }

  private:
    std::span<const AssetSlot> m_slots;
    size_t m_count = 0;
};

/**
 * @brief Every registered asset, keyed by handle and bucketed by type
 *
 * Each type owns a slot array with a free list, so walking one type touches nothing belonging to another and a
 * freed slot is reused rather than shifting the ones after it. Metadata and assets are held by pointer, so their
 * addresses survive a bucket growing.
 */
class AssetStorage {
  public:
    AssetSlot *find(AssetHandle handle);
    const AssetSlot *find(AssetHandle handle) const;

    /**
     * @brief Registers a handle, or replaces the metadata of one already registered
     * @param handle The handle to register
     * @param metadata The metadata, whose assetType picks the bucket
     * @return The slot holding the asset
     */
    AssetSlot &insert(AssetHandle handle, std::unique_ptr<AssetMetadata> metadata);

    /**
     * @brief Drops an asset and returns its slot to the free list
     * @param handle The handle to unregister
     * @return True if the handle was registered
     */
    bool erase(AssetHandle handle);

    void clear();

    /**
     * @brief The live assets of one type
     *
     * Registering an asset of the same type can grow the bucket, which invalidates the range.
     *
     * @param type The type to walk
     * @return A range over that type's slots, holding that type and nothing else
     */
    AssetTypeView ofType(AssetType type) const { return AssetTypeView(m_buckets[type], countOfType(type)); }

    /**
     * @brief Collects the handles of every live asset of one type
     * @param type The type to collect
     * @return The handles, in slot order
     */
    std::vector<AssetHandle> handlesOfType(AssetType type) const;

    size_t countOfType(AssetType type) const { return m_buckets[type].size() - m_freeSlots[type].size(); }
    size_t size() const { return m_index.size(); }

  private:
    AssetSlot &allocate(AssetHandle handle, AssetType type);

    std::unordered_map<AssetHandle, uint32_t> m_index; // handle to a packed {type, slot}
    std::array<std::vector<AssetSlot>, ASSET_TYPE_COUNT> m_buckets;
    std::array<std::vector<uint32_t>, ASSET_TYPE_COUNT> m_freeSlots;
};

} // namespace Rapture

#endif // RAPTURE__ASSET_STORAGE_H

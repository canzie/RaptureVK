#ifndef RAPTURE__BLOB_STORE_H
#define RAPTURE__BLOB_STORE_H

#include "AssetCommon.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <unordered_map>
#include <vector>

namespace Rapture {

/**
 * @brief Append-only store of opaque byte blobs keyed by asset handle, split across partition files
 *
 * Each partition file carries a fixed header plus a uuid to {offset, length} table. The store keeps
 * an in-memory copy of every partition's directory and a checksum, rebuilt from disk on construction.
 * Blob contents are opaque, the owning asset type decides their format.
 */
class BlobStore {
  public:
    /**
     * @brief Opens a blob store rooted at a directory, loading any existing partitions
     * @param directory The directory holding the partition files
     */
    explicit BlobStore(std::filesystem::path directory);

    /**
     * @brief Appends a blob under an id, into a partition with room or a fresh one
     * @param id The asset handle the blob belongs to
     * @param data The bytes to store
     * @return The partition index written to, or -1 on failure
     */
    int32_t store(AssetHandle id, std::span<const uint8_t> data);

    /**
     * @brief Reads back a previously stored blob
     * @param id The asset handle to read
     * @return The blob bytes, or empty on failure
     */
    std::vector<uint8_t> read(AssetHandle id) const;

    bool contains(AssetHandle id) const { return m_idToPartition.find(id) != m_idToPartition.end(); }

  private:
    struct Location {
        uint64_t offset = 0;
        uint64_t length = 0;
    };

    struct Partition {
        std::filesystem::path path;
        uint32_t index = 0;
        uint64_t usedBytes = 0;
        uint32_t checksum = 0;
        std::unordered_map<AssetHandle, Location> entries;
    };

    void loadExistingPartitions();
    bool loadPartition(const std::filesystem::path &path);
    uint32_t acquirePartition(uint64_t neededBytes);
    bool writePartitionDirectory(Partition &partition);
    std::filesystem::path partitionPath(uint32_t index) const;

  private:
    std::filesystem::path m_directory;
    std::vector<Partition> m_partitions;
    std::unordered_map<AssetHandle, uint32_t> m_idToPartition;
};

} // namespace Rapture

#endif // RAPTURE__BLOB_STORE_H

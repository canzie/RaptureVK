// Parked: the packed `.rpack` partition store, the future runtime bake/export format.
//
// It packs many opaque payloads into 64MB partition files keyed by asset handle. It stores no
// registry/metadata, so it must be revamped to carry that before the runtime path is built. Kept
// verbatim for that work and compiled out until then. When revived, re-attach these to a class and
// decide how metadata rides alongside the packed payloads.

#if 0

#include "core/utils/Log.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace Rapture {

static constexpr uint32_t BLOB_PARTITION_MAGIC = 0x424C4252; // "RBLB", identifies a blob partition
static constexpr uint16_t BLOB_PARTITION_VERSION_MAJOR = 1;
static constexpr uint16_t BLOB_PARTITION_VERSION_MINOR = 0;
static constexpr uint32_t BLOB_PARTITION_VERSION =
    (static_cast<uint32_t>(BLOB_PARTITION_VERSION_MAJOR) << 16) | BLOB_PARTITION_VERSION_MINOR;

static constexpr uint64_t BLOB_PARTITION_CAPACITY = 64ull * 1024 * 1024;

// Fixed 64-byte directory at the start of every partition file, mirrored in memory. The reserved
// tail absorbs backward-compatible additions, a breaking change bumps the major version.
struct BlobPartitionHeader {
    uint32_t magic = BLOB_PARTITION_MAGIC;
    uint32_t version = BLOB_PARTITION_VERSION;
    uint32_t partitionIndex = 0;
    uint32_t entryCount = 0;
    uint64_t capacity = BLOB_PARTITION_CAPACITY;
    uint64_t usedBytes = 0;        // bytes of blob data in the data region
    uint64_t entryTableOffset = 0; // byte offset of the uuid to location table
    uint32_t checksum = 0;         // checksum over the entry table bytes
    uint32_t reserved[5] = {};
};

static_assert(sizeof(BlobPartitionHeader) == 64, "blob partition header is a fixed 64-byte directory");

struct BlobEntry {
    uint64_t id = 0;
    uint64_t offset = 0;
    uint64_t length = 0;
};

// FNV-1a over the entry table, enough to catch a truncated or corrupted directory
static uint32_t s_checksum(std::span<const uint8_t> bytes)
{
    uint32_t hash = 2166136261u;
    for (uint8_t b : bytes) {
        hash ^= b;
        hash *= 16777619u;
    }
    return hash;
}

AssetCodec::AssetCodec(std::filesystem::path directory) : m_directory(std::move(directory))
{
    loadExistingPartitions();
}

std::filesystem::path AssetCodec::partitionPath(uint32_t index) const
{
    char name[32];
    std::snprintf(name, sizeof(name), "pack_%04u.rpack", index);
    return m_directory / name;
}

void AssetCodec::loadExistingPartitions()
{
    std::error_code ec;
    if (!std::filesystem::exists(m_directory, ec)) {
        return;
    }

    std::vector<std::filesystem::path> paths;
    for (const auto &entry : std::filesystem::directory_iterator(m_directory, ec)) {
        if (entry.path().extension() == ".rpack") {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());

    for (const auto &path : paths) {
        loadPartition(path);
    }
}

bool AssetCodec::loadPartition(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        RP_CORE_ERROR("Failed to open blob partition '{0}'", path.string());
        return false;
    }

    BlobPartitionHeader header;
    file.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!file || header.magic != BLOB_PARTITION_MAGIC) {
        RP_CORE_ERROR("Blob partition '{0}' has an invalid header", path.string());
        return false;
    }

    uint16_t major = static_cast<uint16_t>(header.version >> 16);
    if (major != BLOB_PARTITION_VERSION_MAJOR) {
        RP_CORE_ERROR("Blob partition '{0}' major version {1} is incompatible with {2}", path.string(), major,
                      BLOB_PARTITION_VERSION_MAJOR);
        return false;
    }

    std::vector<BlobEntry> table(header.entryCount);
    file.seekg(static_cast<std::streamoff>(header.entryTableOffset));
    file.read(reinterpret_cast<char *>(table.data()), static_cast<std::streamsize>(header.entryCount * sizeof(BlobEntry)));
    if (!file) {
        RP_CORE_ERROR("Blob partition '{0}' entry table is truncated", path.string());
        return false;
    }

    std::span<const uint8_t> tableBytes(reinterpret_cast<const uint8_t *>(table.data()), header.entryCount * sizeof(BlobEntry));
    uint32_t checksum = s_checksum(tableBytes);
    if (checksum != header.checksum) {
        RP_CORE_ERROR("Blob partition '{0}' checksum mismatch, skipping", path.string());
        return false;
    }

    Partition partition;
    partition.path = path;
    partition.index = header.partitionIndex;
    partition.usedBytes = header.usedBytes;
    partition.checksum = header.checksum;
    for (const BlobEntry &entry : table) {
        partition.entries[entry.id] = {entry.offset, entry.length};
        m_idToPartition[entry.id] = header.partitionIndex;
    }

    if (partition.index >= m_partitions.size()) {
        m_partitions.resize(partition.index + 1);
    }
    m_partitions[partition.index] = std::move(partition);
    return true;
}

uint32_t AssetCodec::acquirePartition(uint64_t neededBytes)
{
    for (Partition &partition : m_partitions) {
        if (!partition.path.empty() && sizeof(BlobPartitionHeader) + partition.usedBytes + neededBytes <= BLOB_PARTITION_CAPACITY) {
            return partition.index;
        }
    }

    uint32_t index = static_cast<uint32_t>(m_partitions.size());
    Partition partition;
    partition.path = partitionPath(index);
    partition.index = index;
    m_partitions.push_back(std::move(partition));
    return index;
}

bool AssetCodec::writePartitionDirectory(Partition &partition)
{
    std::vector<BlobEntry> table;
    table.reserve(partition.entries.size());
    for (const auto &[id, location] : partition.entries) {
        table.push_back({id, location.offset, location.length});
    }

    std::span<const uint8_t> tableBytes(reinterpret_cast<const uint8_t *>(table.data()), table.size() * sizeof(BlobEntry));
    partition.checksum = s_checksum(tableBytes);

    BlobPartitionHeader header;
    header.partitionIndex = partition.index;
    header.entryCount = static_cast<uint32_t>(table.size());
    header.usedBytes = partition.usedBytes;
    header.entryTableOffset = sizeof(BlobPartitionHeader) + partition.usedBytes;
    header.checksum = partition.checksum;

    std::fstream file(partition.path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file) {
        file.open(partition.path, std::ios::out | std::ios::binary);
    }
    if (!file) {
        RP_CORE_ERROR("Failed to open blob partition '{0}' for writing", partition.path.string());
        return false;
    }

    file.write(reinterpret_cast<const char *>(&header), sizeof(header));
    file.seekp(static_cast<std::streamoff>(header.entryTableOffset));
    file.write(reinterpret_cast<const char *>(table.data()), static_cast<std::streamsize>(tableBytes.size()));
    if (!file) {
        RP_CORE_ERROR("Failed to write blob partition directory '{0}'", partition.path.string());
        return false;
    }

    return true;
}

int32_t AssetCodec::store(AssetHandle id, std::span<const uint8_t> data)
{
    auto existing = m_idToPartition.find(id);
    if (existing != m_idToPartition.end()) {
        RP_CORE_WARN("Blob for asset {0} already stored, keeping the existing copy", id);
        return static_cast<int32_t>(existing->second);
    }

    uint32_t index = acquirePartition(data.size());
    Partition &partition = m_partitions[index];

    uint64_t dataOffset = sizeof(BlobPartitionHeader) + partition.usedBytes;

    std::fstream file(partition.path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file) {
        file.open(partition.path, std::ios::out | std::ios::binary);
    }
    if (!file) {
        RP_CORE_ERROR("Failed to open blob partition '{0}'", partition.path.string());
        return -1;
    }

    file.seekp(static_cast<std::streamoff>(dataOffset));
    file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    file.close();

    partition.entries[id] = {dataOffset, data.size()};
    partition.usedBytes += data.size();

    if (!writePartitionDirectory(partition)) {
        partition.entries.erase(id);
        partition.usedBytes -= data.size();
        return -1;
    }

    m_idToPartition[id] = index;
    return static_cast<int32_t>(index);
}

std::vector<uint8_t> AssetCodec::read(AssetHandle id) const
{
    auto partitionIt = m_idToPartition.find(id);
    if (partitionIt == m_idToPartition.end()) {
        RP_CORE_WARN("No blob stored for asset {0}", id);
        return {};
    }

    const Partition &partition = m_partitions[partitionIt->second];
    auto entryIt = partition.entries.find(id);
    if (entryIt == partition.entries.end()) {
        RP_CORE_ERROR("Blob directory for asset {0} is inconsistent", id);
        return {};
    }
    const Location &location = entryIt->second;

    std::ifstream file(partition.path, std::ios::binary);
    if (!file) {
        RP_CORE_ERROR("Failed to open blob partition '{0}'", partition.path.string());
        return {};
    }

    std::vector<uint8_t> data(location.length);
    file.seekg(static_cast<std::streamoff>(location.offset));
    file.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(location.length));
    if (!file) {
        RP_CORE_ERROR("Failed to read blob for asset {0}", id);
        return {};
    }

    return data;
}

} // namespace Rapture

#endif

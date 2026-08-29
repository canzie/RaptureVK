#include "AssetCodec.h"

#include "Asset.h"
#include "core/utils/Log.h"
#include "scene/instances/InstanceRegistry.h"

#include <cstring>
#include <fstream>
#include <string_view>

namespace Rapture {

/**
 * @brief Whether an asset type names the authored class it holds
 * @param type The asset type to test
 * @return True if the type records a class name in its metadata
 */
static bool s_recordsClass(AssetType type)
{
    return type == ASSET_MODULE;
}

static constexpr uint32_t RASSET_MAGIC = 0x54534152; // "RAST"
static constexpr uint16_t RASSET_VERSION_MAJOR = 1;
static constexpr uint16_t RASSET_VERSION_MINOR = 0;
static constexpr uint32_t RASSET_VERSION = (static_cast<uint32_t>(RASSET_VERSION_MAJOR) << 16) | RASSET_VERSION_MINOR;

// Fixed 64-byte header at the start of every asset file. The metadata section follows immediately,
// then the payload. The reserved tail absorbs backward-compatible additions, a breaking change
// bumps the major version.
struct RaptureAssetHeader {
    uint32_t magic = RASSET_MAGIC;
    uint32_t version = RASSET_VERSION;
    uint64_t uuid = 0;
    uint32_t assetTypeCode = 0;
    uint32_t flags = 0;
    uint64_t metadataSize = 0;
    uint64_t payloadSize = 0;
    uint32_t metadataChecksum = 0;
    uint32_t payloadChecksum = 0;
    uint32_t reserved[4] = {};
};

static_assert(sizeof(RaptureAssetHeader) == 64, "the asset file header is a fixed 64-byte block");

// FNV-1a, enough to catch a truncated or corrupted section
static uint32_t s_checksum(std::span<const uint8_t> bytes)
{
    uint32_t hash = 2166136261u;
    for (uint8_t b : bytes) {
        hash ^= b;
        hash *= 16777619u;
    }
    return hash;
}

template <typename T>
static void s_append(std::vector<uint8_t> &out, const T &value)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

static void s_appendString(std::vector<uint8_t> &out, std::string_view str)
{
    s_append(out, static_cast<uint32_t>(str.size()));
    out.insert(out.end(), str.begin(), str.end());
}

struct ByteReader {
    const uint8_t *data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    bool ok = true;

    template <typename T>
    T read()
    {
        T value{};
        if (pos + sizeof(T) > size) {
            ok = false;
            return value;
        }
        std::memcpy(&value, data + pos, sizeof(T));
        pos += sizeof(T);
        return value;
    }

    std::string readString()
    {
        uint32_t length = read<uint32_t>();
        if (!ok || pos + length > size) {
            ok = false;
            return {};
        }
        std::string str(reinterpret_cast<const char *>(data + pos), length);
        pos += length;
        return str;
    }
};

// TODO: importConfig (variant) is not encoded yet; assets reload from the payload, reimport config comes later
static std::vector<uint8_t> s_serializeMetadata(const AssetMetadata &metadata)
{
    std::vector<uint8_t> out;
    s_append(out, AssetTypeToCode(metadata.assetType));
    s_append(out, static_cast<uint32_t>(metadata.storageType));
    s_append(out, static_cast<uint32_t>(metadata.evictionPolicy));
    s_append(out, metadata.sizeHintBytes);
    s_appendString(out, metadata.name);

    bool hasProvenance = metadata.provenance.has_value();
    s_append(out, static_cast<uint8_t>(hasProvenance ? 1 : 0));
    if (hasProvenance) {
        s_appendString(out, metadata.provenance->sourcePath.generic_string());
        bool hasSubIndex = metadata.provenance->sourceSubIndex.has_value();
        s_append(out, static_cast<uint8_t>(hasSubIndex ? 1 : 0));
        s_append(out, hasSubIndex ? *metadata.provenance->sourceSubIndex : uint32_t{0});
    }

    // only the authored types record a class, so no file written before the type existed is asked to read one
    if (s_recordsClass(metadata.assetType)) {
        s_appendString(out, metadata.authoredClass != nullptr ? metadata.authoredClass->name : std::string_view{});
    }
    return out;
}

static std::unique_ptr<AssetMetadata> s_deserializeMetadata(std::span<const uint8_t> bytes)
{
    ByteReader reader{bytes.data(), bytes.size()};
    auto metadata = std::make_unique<AssetMetadata>();
    metadata->assetType = AssetTypeFromCode(reader.read<uint32_t>());
    metadata->storageType = static_cast<AssetStorageType>(reader.read<uint32_t>());
    metadata->evictionPolicy = static_cast<AssetEvictionPolicy>(reader.read<uint32_t>());
    metadata->sizeHintBytes = reader.read<uint64_t>();
    metadata->name = reader.readString();

    if (reader.read<uint8_t>() != 0) {
        AssetProvenance provenance;
        provenance.sourcePath = reader.readString();
        bool hasSubIndex = reader.read<uint8_t>() != 0;
        uint32_t subIndex = reader.read<uint32_t>();
        if (hasSubIndex) {
            provenance.sourceSubIndex = subIndex;
        }
        metadata->provenance = std::move(provenance);
    }

    if (s_recordsClass(metadata->assetType)) {
        std::string className = reader.readString();
        metadata->authoredClass = InstanceRegistry::find(className);
        if (metadata->authoredClass == nullptr) {
            RP_CORE_WARN("no class named '{}', '{}' cannot be loaded", className, metadata->name);
        }
    }

    if (!reader.ok) {
        return nullptr;
    }
    return metadata;
}

bool AssetCodec::writeRaptureAsset(const std::filesystem::path &path, AssetHandle uuid, const AssetMetadata &metadata,
                                   std::span<const uint8_t> payload)
{
    std::vector<uint8_t> metadataBytes = s_serializeMetadata(metadata);

    RaptureAssetHeader header;
    header.uuid = uuid;
    header.assetTypeCode = AssetTypeToCode(metadata.assetType);
    header.metadataSize = metadataBytes.size();
    header.payloadSize = payload.size();
    header.metadataChecksum = s_checksum(metadataBytes);
    header.payloadChecksum = s_checksum(payload);

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        RP_CORE_ERROR("Failed to open '{0}' for writing", path.string());
        return false;
    }

    file.write(reinterpret_cast<const char *>(&header), sizeof(header));
    file.write(reinterpret_cast<const char *>(metadataBytes.data()), static_cast<std::streamsize>(metadataBytes.size()));
    file.write(reinterpret_cast<const char *>(payload.data()), static_cast<std::streamsize>(payload.size()));
    if (!file) {
        RP_CORE_ERROR("Failed to write '{0}'", path.string());
        return false;
    }

    return true;
}

static bool s_readHeader(std::ifstream &file, const std::filesystem::path &path, RaptureAssetHeader &header)
{
    file.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!file || header.magic != RASSET_MAGIC) {
        RP_CORE_ERROR("Rasset '{0}' has an invalid header", path.string());
        return false;
    }

    uint16_t major = static_cast<uint16_t>(header.version >> 16);
    if (major != RASSET_VERSION_MAJOR) {
        RP_CORE_ERROR("Rasset '{0}' major version {1} is incompatible with {2}", path.string(), major, RASSET_VERSION_MAJOR);
        return false;
    }
    return true;
}

std::optional<AssetCodec::RaptureAssetInfo> AssetCodec::readRaptureAssetInfo(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        RP_CORE_ERROR("Failed to open '{0}'", path.string());
        return std::nullopt;
    }

    RaptureAssetHeader header;
    if (!s_readHeader(file, path, header)) {
        return std::nullopt;
    }

    std::vector<uint8_t> metadataBytes(header.metadataSize);
    file.read(reinterpret_cast<char *>(metadataBytes.data()), static_cast<std::streamsize>(header.metadataSize));
    if (!file) {
        RP_CORE_ERROR("Rasset '{0}' metadata is truncated", path.string());
        return std::nullopt;
    }

    if (s_checksum(metadataBytes) != header.metadataChecksum) {
        RP_CORE_ERROR("Rasset '{0}' metadata checksum mismatch", path.string());
        return std::nullopt;
    }

    std::unique_ptr<AssetMetadata> metadata = s_deserializeMetadata(metadataBytes);
    if (!metadata) {
        RP_CORE_ERROR("Rasset '{0}' metadata is malformed", path.string());
        return std::nullopt;
    }

    return RaptureAssetInfo{header.uuid, std::move(metadata)};
}

std::vector<uint8_t> AssetCodec::readRaptureAssetPayload(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        RP_CORE_ERROR("Failed to open '{0}'", path.string());
        return {};
    }

    RaptureAssetHeader header;
    if (!s_readHeader(file, path, header)) {
        return {};
    }

    std::vector<uint8_t> payload(header.payloadSize);
    file.seekg(static_cast<std::streamoff>(sizeof(RaptureAssetHeader) + header.metadataSize));
    file.read(reinterpret_cast<char *>(payload.data()), static_cast<std::streamsize>(header.payloadSize));
    if (!file) {
        RP_CORE_ERROR("Rasset '{0}' payload is truncated", path.string());
        return {};
    }

    if (s_checksum(payload) != header.payloadChecksum) {
        RP_CORE_ERROR("Rasset '{0}' payload checksum mismatch", path.string());
        return {};
    }

    return payload;
}

} // namespace Rapture

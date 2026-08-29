#ifndef RAPTURE__ASSET_CODEC_H
#define RAPTURE__ASSET_CODEC_H

#include "AssetCommon.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Rapture {

struct AssetMetadata;

/**
 * @brief Reads and writes Rapture's binary asset formats
 *
 * Currently handles the loose per-asset file, the one each type is written with its own extension:
 * a self-contained container of a fixed header, the asset's metadata record and the type's cooked
 * payload. It owns the header and metadata encoding; the payload is opaque bytes the owning asset
 * type serializes, and the header's type code is what a file is identified by rather than its
 * extension. A loose asset file never references another file. The packed `.rpack` partition form
 * for the runtime bake is parked (see AssetPack.cpp) until that path is built.
 */
class AssetCodec {
  public:
    struct RaptureAssetInfo {
        AssetHandle uuid = INVALID_ASSET_HANDLE;
        std::unique_ptr<AssetMetadata> metadata;
    };

    /**
     * @brief Writes a self-contained asset file, overwriting any existing file at the path
     * @param path The destination file
     * @param uuid The asset handle stored in the header, the asset's identity
     * @param metadata The metadata record encoded into the file
     * @param payload The cooked asset bytes read back on load
     * @return True on success, false on an I/O failure
     */
    static bool writeRaptureAsset(const std::filesystem::path &path, AssetHandle uuid, const AssetMetadata &metadata,
                                  std::span<const uint8_t> payload);

    /**
     * @brief Reads the header and metadata of an asset file without touching the payload, for the scan
     * @param path The file to read
     * @return The uuid and decoded metadata, or empty on failure
     */
    static std::optional<RaptureAssetInfo> readRaptureAssetInfo(const std::filesystem::path &path);

    /**
     * @brief Reads the payload section of an asset file, for loading the asset
     * @param path The file to read
     * @return The payload bytes, or empty on failure
     */
    static std::vector<uint8_t> readRaptureAssetPayload(const std::filesystem::path &path);
};

} // namespace Rapture

#endif // RAPTURE__ASSET_CODEC_H

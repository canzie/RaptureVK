#ifndef RAPTURE__ASSETCOMMON_H
#define RAPTURE__ASSETCOMMON_H

#include "core/utils/UUID.h"

#include <filesystem>
#include <optional>

namespace Rapture {

using AssetHandle = UUID;

static constexpr AssetHandle INVALID_ASSET_HANDLE = 0;

/**
 * @brief Where an asset was originally imported from, kept for reimport not for loading
 */
struct AssetProvenance {
    std::filesystem::path sourcePath;                      // original file, empty if generated
    std::optional<uint32_t> sourceSubIndex = std::nullopt; // sub-asset index within a container source, e.g. a glTF mesh
};

enum AssetType {
    ASSET_NONE,
    ASSET_TEXTURE,
    ASSET_CUBEMAP,
    ASSET_SHADER,
    ASSET_MATERIAL,
    ASSET_MATERIAL_INSTANCE,
    ASSET_STATIC_MESH,
    ASSET_SKELETAL_MESH,
    ASSET_MODULE,
    ASSET_SKELETON,
    ASSET_ANIMATION,
    ASSET_AUDIO,
    ASSET_VIDEO,
    ASSET_WORLD,
    ASSET_TYPE_COUNT
};

enum class AssetStorageType {
    DISK,
    VIRTUAL
};

enum class AssetStatus {
    REQUESTED,
    LOADING,
    LOADED,
    FAILED,
    FILE_NOT_FOUND
};

enum class AssetEvictionPolicy {
    EVICT_IMMEDIATE, // evict as soon as useCount hits 0
    EVICT_HINT_LAZY, // useCount 0 -> cold LRU list, freed under memory-budget pressure
    EVICT_HINT_LAST  // "keep loaded" hint, evicted last under pressure, never a guarantee
};

/**
 * @brief Packs four characters into the code a type is written as
 * @param code The four characters, as a string literal
 * @return The packed code
 */
inline constexpr uint32_t Asset_fourCC(const char (&code)[5])
{
    return static_cast<uint32_t>(code[0]) | (static_cast<uint32_t>(code[1]) << 8) | (static_cast<uint32_t>(code[2]) << 16) |
           (static_cast<uint32_t>(code[3]) << 24);
}

} // namespace Rapture
#endif // RAPTURE__ASSETCOMMON_H

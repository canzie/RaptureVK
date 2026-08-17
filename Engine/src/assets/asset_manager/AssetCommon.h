#ifndef RAPTURE__ASSETCOMMON_H
#define RAPTURE__ASSETCOMMON_H

#include "core/utils/UUID.h"
#include "core/utils/rp_assert.h"

#include <filesystem>
#include <optional>
#include <string>

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
    ASSET_SCENE_OBJECT,
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

struct AssetTypeCode {
    AssetType type;
    uint32_t code;
};

/**
 * @brief What each asset type is written as on disk.
 *
 * A code is the type's identity in a file rather than its place in the enum, so the enum is free to
 * gain, lose and reorder entries. A code is never reused for a different type.
 */
inline constexpr AssetTypeCode ASSET_TYPE_CODES[] = {
    {ASSET_TEXTURE, Asset_fourCC("TEX ")},
    {ASSET_CUBEMAP, Asset_fourCC("CUBE")},
    {ASSET_SHADER, Asset_fourCC("SHDR")},
    {ASSET_MATERIAL, Asset_fourCC("MTL ")},
    {ASSET_MATERIAL_INSTANCE, Asset_fourCC("MTLI")},
    {ASSET_STATIC_MESH, Asset_fourCC("MESH")},
    {ASSET_SKELETAL_MESH, Asset_fourCC("SKMH")},
    {ASSET_SCENE_OBJECT, Asset_fourCC("SOBJ")},
    {ASSET_SKELETON, Asset_fourCC("SKEL")},
    {ASSET_ANIMATION, Asset_fourCC("ANIM")},
    {ASSET_AUDIO, Asset_fourCC("AUD ")},
    {ASSET_VIDEO, Asset_fourCC("VID ")},
    {ASSET_WORLD, Asset_fourCC("WRLD")},
};

/**
 * @brief The code a type is written as
 * @param type The type to look up
 * @return The code
 */
inline uint32_t AssetTypeToCode(AssetType type)
{
    for (const AssetTypeCode &entry : ASSET_TYPE_CODES) {
        if (entry.type == type) {
            return entry.code;
        }
    }

    RP_ASSERT(false, "asset type {} has no code to be written as", static_cast<int>(type));
    RP_UNREACHABLE();
}

/**
 * @brief The type a code names
 * @param code The code read from a file
 * @return The type, or ASSET_NONE if no type is written as that code
 */
inline AssetType AssetTypeFromCode(uint32_t code)
{
    for (const AssetTypeCode &entry : ASSET_TYPE_CODES) {
        if (entry.code == code) {
            return entry.type;
        }
    }
    return ASSET_NONE;
}

inline std::string AssetTypeToString(AssetType type)
{
    switch (type) {
    case ASSET_TEXTURE:
        return "Texture";
    case ASSET_CUBEMAP:
        return "Cubemap";
    case ASSET_SHADER:
        return "Shader";
    case ASSET_MATERIAL:
        return "Material";
    case ASSET_MATERIAL_INSTANCE:
        return "Material Instance";
    case ASSET_STATIC_MESH:
        return "Static Mesh";
    case ASSET_SKELETAL_MESH:
        return "Skeletal Mesh";
    case ASSET_SCENE_OBJECT:
        return "Scene Object";
    case ASSET_SKELETON:
        return "Skeleton";
    case ASSET_ANIMATION:
        return "Animation";
    case ASSET_AUDIO:
        return "Audio";
    case ASSET_VIDEO:
        return "Video";
    case ASSET_WORLD:
        return "World";
    default:
        return "Unknown";
    }
}

} // namespace Rapture
#endif // RAPTURE__ASSETCOMMON_H

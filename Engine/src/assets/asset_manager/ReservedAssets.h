#ifndef RAPTURE__RESERVED_ASSETS_H
#define RAPTURE__RESERVED_ASSETS_H

#include "AssetCommon.h"

namespace Rapture {

/**
 * @brief Fixed handles for engine builtins
 */
enum ReservedAsset : AssetHandle {
    RE_NONE = 0,
    RE_WHITE_TEXTURE,
    RE_FLAT_NORMAL_TEXTURE,
    RE_DEFAULT_MATERIAL,
    RE_DEFAULT_MATERIAL_INSTANCE,
    RE_TERRAIN_MATERIAL,
    RE_GLTF_BASE_MATERIAL,
    RE_PRIMITIVE_CUBE_MESH,
    RE_PRIMITIVE_SPHERE_MESH,
    RE_PRIMITIVE_PLANE_MESH,
    RE_GRID_MATERIAL,
    RE_GRID_MATERIAL_INSTANCE,
    RE_COUNT
};

static constexpr AssetHandle RESERVED_ASSET_LIMIT = 1024;

static_assert(RE_COUNT < RESERVED_ASSET_LIMIT, "reserved asset ids must stay below the reserved range");

/**
 * @brief Whether a handle belongs to an engine builtin
 * @param handle The handle to test
 * @return True if the handle is in the reserved range
 */
inline bool Asset_isReserved(AssetHandle handle)
{
    return handle != RE_NONE && handle < RE_COUNT;
}

} // namespace Rapture

#endif // RAPTURE__RESERVED_ASSETS_H

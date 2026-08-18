#ifndef RAPTURE__MATERIAL_DATA_H
#define RAPTURE__MATERIAL_DATA_H

#include <cstdint>

namespace Rapture {

// ============================================================================
// Material Flags - must match MaterialCommon.glsl
// ============================================================================

enum MaterialFlags {
    // Vertex attribute flags (bits 0-4), set from the mesh buffer layout
    MAT_FLAG_HAS_NORMALS = 1u << 0,
    MAT_FLAG_HAS_TANGENTS = 1u << 1,
    MAT_FLAG_HAS_BITANGENTS = 1u << 2,
    MAT_FLAG_HAS_TEXCOORDS = 1u << 3,
};

inline bool hasFlag(uint32_t flags, uint32_t flag)
{
    return (flags & flag) != 0;
}

// Per-material header indexed by material id. Surface inputs live in the graph slice at
// graphDataOffset.
struct MaterialData {
    uint32_t flags;
    uint32_t graphId;
    uint32_t graphDataOffset;

    /**
     * @brief A header with no bound graph
     * @return The default material header
     */
    static MaterialData createDefault() { return MaterialData{}; }
};

static_assert(sizeof(MaterialData) == 12, "MaterialData must be 12 bytes to match its std430 mirror");

} // namespace Rapture

#endif // RAPTURE__MATERIAL_DATA_H

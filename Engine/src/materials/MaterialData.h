#ifndef RAPTURE__MATERIAL_DATA_H
#define RAPTURE__MATERIAL_DATA_H

#include <cstdint>

namespace Rapture {

// ============================================================================
// Material Flags - must match MaterialCommon.glsl
// ============================================================================

enum MaterialFlags {
    // Vertex attribute flags (bits 0-4)
    MAT_FLAG_HAS_NORMALS = 1u << 0,
    MAT_FLAG_HAS_TANGENTS = 1u << 1,
    MAT_FLAG_HAS_BITANGENTS = 1u << 2,
    MAT_FLAG_HAS_TEXCOORDS = 1u << 3,

    // Texture map flags (bits 5-15)
    MAT_FLAG_HAS_ALBEDO_MAP = 1u << 5,
    MAT_FLAG_HAS_NORMAL_MAP = 1u << 6,
    MAT_FLAG_HAS_METALLIC_ROUGHNESS_MAP = 1u << 7,
    MAT_FLAG_HAS_AO_MAP = 1u << 8,
    MAT_FLAG_HAS_METALLIC_MAP = 1u << 9,
    MAT_FLAG_HAS_ROUGHNESS_MAP = 1u << 10,
    MAT_FLAG_HAS_EMISSIVE_MAP = 1u << 11,
    MAT_FLAG_HAS_SPECULAR_MAP = 1u << 12,
    MAT_FLAG_HAS_HEIGHT_MAP = 1u << 13,

    // Normal map is BC5-compressed (RG only), reconstruct Z in the shader
    MAT_FLAG_NORMAL_BC5 = 1u << 14,
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

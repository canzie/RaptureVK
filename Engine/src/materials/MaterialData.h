#ifndef RAPTURE__MATERIAL_DATA_H
#define RAPTURE__MATERIAL_DATA_H

#include <cstdint>
#include <glm/glm.hpp>

namespace Rapture {

// ============================================================================
// Material Flags - must match MaterialCommon.glsl
// ============================================================================

enum MaterialFlags : uint32_t {
    // Vertex attribute flags (bits 0-4)
    MAT_FLAG_HAS_NORMALS    = 1u << 0,
    MAT_FLAG_HAS_TANGENTS   = 1u << 1,
    MAT_FLAG_HAS_BITANGENTS = 1u << 2,
    MAT_FLAG_HAS_TEXCOORDS  = 1u << 3,

    // Texture map flags (bits 5-15)
    MAT_FLAG_HAS_ALBEDO_MAP             = 1u << 5,
    MAT_FLAG_HAS_NORMAL_MAP             = 1u << 6,
    MAT_FLAG_HAS_METALLIC_ROUGHNESS_MAP = 1u << 7,
    MAT_FLAG_HAS_AO_MAP                 = 1u << 8,
    MAT_FLAG_HAS_METALLIC_MAP           = 1u << 9,
    MAT_FLAG_HAS_ROUGHNESS_MAP          = 1u << 10,
    MAT_FLAG_HAS_EMISSIVE_MAP           = 1u << 11,
    MAT_FLAG_HAS_SPECULAR_MAP           = 1u << 12,
    MAT_FLAG_HAS_HEIGHT_MAP             = 1u << 13,

    // Material type flags (bits 16-19)
    MAT_FLAG_IS_TERRAIN     = 1u << 16,
    MAT_FLAG_HAS_SPLAT_MAP  = 1u << 17,
    MAT_FLAG_USE_TRIPLANAR  = 1u << 18,
};

inline bool hasFlag(uint32_t flags, uint32_t flag) {
    return (flags & flag) != 0;
}

// ============================================================================
// Material Data Struct - 96 bytes, std140 compatible
// Must match MaterialCommon.glsl exactly
// ============================================================================

struct MaterialData {
    glm::vec4 albedo;              // 0-16: rgb = albedo, a = alpha

    float roughness;               // 16-20
    float metallic;                // 20-24
    float ao;                      // 24-28
    uint32_t flags;                // 28-32

    glm::vec4 emissive;            // 32-48: rgb = color, a = strength

    glm::uvec4 texIndices0;        // 48-64: albedo, normal, metallicRoughness, ao
    glm::uvec4 texIndices1;        // 64-80: emissive, height, specular, splatMap

    float tilingScale;             // 80-84
    float heightBlend;             // 84-88
    float slopeThreshold;          // 88-92
    float _pad;                    // 92-96

    // Returns a MaterialData with sensible defaults
    // defaultTexIndex should be the bindless index of a 1x1 white texture
    static MaterialData createDefault(uint32_t defaultTexIndex) {
        MaterialData data{};
        data.albedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        data.roughness = 0.5f;
        data.metallic = 0.0f;
        data.ao = 1.0f;
        data.flags = 0;
        data.emissive = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        data.texIndices0 = glm::uvec4(defaultTexIndex);
        data.texIndices1 = glm::uvec4(defaultTexIndex);
        data.tilingScale = 1.0f;
        data.heightBlend = 0.5f;
        data.slopeThreshold = 0.7f;
        data._pad = 0.0f;
        return data;
    }
};

static_assert(sizeof(MaterialData) == 96, "MaterialData must be 96 bytes for std140 compatibility");

// ============================================================================
// Texture Index Helpers
// ============================================================================

// Indices into texIndices0
constexpr uint32_t TEX_IDX_ALBEDO = 0;
constexpr uint32_t TEX_IDX_NORMAL = 1;
constexpr uint32_t TEX_IDX_METALLIC_ROUGHNESS = 2;
constexpr uint32_t TEX_IDX_AO = 3;

// Indices into texIndices1
constexpr uint32_t TEX_IDX_EMISSIVE = 0;
constexpr uint32_t TEX_IDX_HEIGHT = 1;
constexpr uint32_t TEX_IDX_SPECULAR = 2;
constexpr uint32_t TEX_IDX_SPLAT_MAP = 3;

} // namespace Rapture

#endif // RAPTURE__MATERIAL_DATA_H

// MaterialCommon.glsl - Static material layout used by all shaders
// The C++ MaterialData struct must match this exactly.

#ifndef MATERIAL_COMMON_GLSL
#define MATERIAL_COMMON_GLSL

// ============================================================================
// Material Flags
// ============================================================================

const uint MAT_FLAG_HAS_NORMALS    = 1u << 0;
const uint MAT_FLAG_HAS_TANGENTS   = 1u << 1;
const uint MAT_FLAG_HAS_BITANGENTS = 1u << 2;
const uint MAT_FLAG_HAS_TEXCOORDS  = 1u << 3;

const uint MAT_FLAG_HAS_ALBEDO_MAP             = 1u << 5;
const uint MAT_FLAG_HAS_NORMAL_MAP             = 1u << 6;
const uint MAT_FLAG_HAS_METALLIC_ROUGHNESS_MAP = 1u << 7;
const uint MAT_FLAG_HAS_AO_MAP                 = 1u << 8;
const uint MAT_FLAG_HAS_METALLIC_MAP           = 1u << 9;
const uint MAT_FLAG_HAS_ROUGHNESS_MAP          = 1u << 10;
const uint MAT_FLAG_HAS_EMISSIVE_MAP           = 1u << 11;
const uint MAT_FLAG_HAS_SPECULAR_MAP           = 1u << 12;
const uint MAT_FLAG_HAS_HEIGHT_MAP             = 1u << 13;

const uint MAT_FLAG_IS_TERRAIN     = 1u << 16;
const uint MAT_FLAG_HAS_SPLAT_MAP  = 1u << 17;
const uint MAT_FLAG_USE_TRIPLANAR  = 1u << 18;

// ============================================================================
// Material Data Struct - 96 bytes, std140 compatible
// Texture indices default to 0 (white texture) when not used
// ============================================================================

struct MaterialData {
    vec4 albedo;               // 0-16: rgb = albedo, a = alpha

    float roughness;           // 16-20
    float metallic;            // 20-24
    float ao;                  // 24-28
    uint flags;                // 28-32

    vec4 emissive;             // 32-48: rgb = color, a = strength

    uvec4 texIndices0;         // 48-64: albedo, normal, metallicRoughness, ao
    uvec4 texIndices1;         // 64-80: emissive, height, specular, splatMap

    float tilingScale;         // 80-84
    float heightBlend;         // 84-88
    float slopeThreshold;      // 88-92
    float _pad;                // 92-96
};

// ============================================================================
// Helpers
// ============================================================================

bool matHasFlag(uint flags, uint flag) {
    return (flags & flag) != 0u;
}

float matFlagMul(uint flags, uint flag) {
    return float((flags & flag) != 0u);
}

// Sampling macros - TEXTURES must be the bindless sampler2D array declared in the shader
#define SAMPLE_ALBEDO(mat, TEXTURES, uv) \
    (mat.albedo.rgb * mix(vec3(1.0), texture(TEXTURES[mat.texIndices0.x], uv).rgb, matFlagMul(mat.flags, MAT_FLAG_HAS_ALBEDO_MAP)))

#define SAMPLE_ROUGHNESS(mat, TEXTURES, uv) \
    (mat.roughness * mix(1.0, texture(TEXTURES[mat.texIndices0.z], uv).g, matFlagMul(mat.flags, MAT_FLAG_HAS_METALLIC_ROUGHNESS_MAP)))

#define SAMPLE_METALLIC(mat, TEXTURES, uv) \
    (mat.metallic * mix(1.0, texture(TEXTURES[mat.texIndices0.z], uv).b, matFlagMul(mat.flags, MAT_FLAG_HAS_METALLIC_ROUGHNESS_MAP)))

#define SAMPLE_AO(mat, TEXTURES, uv) \
    (mat.ao * mix(1.0, texture(TEXTURES[mat.texIndices0.w], uv).r, matFlagMul(mat.flags, MAT_FLAG_HAS_AO_MAP)))

#define SAMPLE_NORMAL_MAP(mat, TEXTURES, uv) \
    mix(vec3(0.0, 0.0, 1.0), texture(TEXTURES[mat.texIndices0.y], uv).xyz * 2.0 - 1.0, matFlagMul(mat.flags, MAT_FLAG_HAS_NORMAL_MAP))

#define SAMPLE_EMISSIVE(mat, TEXTURES, uv) \
    (mat.emissive.rgb * mat.emissive.a * mix(vec3(1.0), texture(TEXTURES[mat.texIndices1.x], uv).rgb, matFlagMul(mat.flags, MAT_FLAG_HAS_EMISSIVE_MAP)))

#endif // MATERIAL_COMMON_GLSL

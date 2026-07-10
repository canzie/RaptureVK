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
const uint MAT_FLAG_NORMAL_BC5                 = 1u << 14;

const uint MAT_FLAG_IS_TERRAIN     = 1u << 16;
const uint MAT_FLAG_HAS_SPLAT_MAP  = 1u << 17;
const uint MAT_FLAG_USE_TRIPLANAR  = 1u << 18;
const uint MAT_FLAG_IS_GRAPH       = 1u << 19;

// Per-material header, mirror of MaterialData.h. Surface inputs live in the graph slice at
// graphDataOffset; the terrain scalars are kept until the terrain path is removed.
struct MaterialData {
    uint flags;
    uint graphId;
    uint graphDataOffset;
    float tilingScale;
    float heightBlend;
    float slopeThreshold;
};

// ============================================================================
// Material Data Buffer - one SSBO arena indexed by material id (set 1, binding 0)
// std430 layout matches the std140 struct byte-for-byte (no inner arrays)
// ============================================================================

layout(std430, set = 1, binding = 0) readonly buffer MaterialDataBuffer {
    MaterialData materials[];
} u_materialBuffer;

MaterialData getMaterialData(uint index) {
    return u_materialBuffer.materials[index];
}

// ============================================================================
// Graph material data - one flat uint arena (set 1, binding 1)
// A generated evalSurface_* reads its instance slice at MaterialData.graphDataOffset;
// the compiler bakes each value's offset and unpacks it (uintBitsToFloat for floats, raw for uints)
// ============================================================================

layout(std430, set = 1, binding = 1) readonly buffer GraphPoolBuffer {
    uint data[];
} u_graphPool;

// Bindless texture array sampled by the generated surface graph texture nodes (set 3, binding 0)
layout(set = 3, binding = 0) uniform sampler2D u_textures[];

#include "common/ShadingModels.glsl"

// ============================================================================
// Surface interface - what a surface eval reads (inputs) and writes (outputs)
// A static material or a generated graph both fill a SurfaceData for the G-buffer
// ============================================================================

struct SurfaceInputs {
    vec2 uv;
    vec3 worldPos;
    vec3 worldNormal;   // interpolated geometric normal, not normalized
    vec3 tangent;
    vec3 bitangent;
    uint flags;
};

struct SurfaceData {
    vec3 albedo;
    vec3 normal;        // world space
    float roughness;
    float metallic;
    float ao;
    vec4 emission;
    float emissiveStrength;
    uint shadingModelId;
};

struct SurfaceDataDiffuse {
    vec3 albedo;
    vec3 normal;        // world space
    vec4 emission;
    float emissiveStrength;
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

vec2 signNotZero(vec2 v) {
    return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

// Octahedral encode a unit normal into a 2-component value in [-1, 1]
vec2 octEncodeNormal(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 enc = (n.z >= 0.0) ? n.xy : (1.0 - abs(n.yx)) * signNotZero(n.xy);
    return enc;
}

// Decode an octahedral-encoded normal back to a unit vector
vec3 octDecodeNormal(vec2 enc) {
    vec3 n = vec3(enc.xy, 1.0 - abs(enc.x) - abs(enc.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * signNotZero(n.xy);
    }
    return normalize(n);
}

// Reconstruct a tangent-space normal's Z from its XY (unit-length assumption), for BC5 normal maps
vec3 reconstructNormalZ(vec2 xy) {
    vec2 n = xy * 2.0 - 1.0;
    float z = sqrt(max(0.0, 1.0 - dot(n, n)));
    return vec3(n, z);
}

#endif // MATERIAL_COMMON_GLSL

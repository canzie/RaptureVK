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

// Per-material header, mirror of MaterialData.h. Surface inputs live in the graph slice at
// graphDataOffset.
struct MaterialData {
    uint flags;
    uint graphId;
    uint graphDataOffset;
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
// Surface interface - the inputs a surface eval reads
// The per-pass output structs (SurfaceData, SurfaceDataDiffuse) are generated into their pass files
// ============================================================================

struct SurfaceInputs {
    vec2 uv;
    vec3 worldPos;
    vec3 worldNormal;   // interpolated geometric normal, not normalized
    vec3 tangent;
    vec3 bitangent;
    uint flags;
};

// Terrain has no vertex attributes, so it carries no tangent frame and no mesh uv. The noise field
// indices are sampled lazily by the terrain graph nodes that read them.
struct TerrainInputs {
    vec2 uv;                  // global terrain uv, 0..1 across the whole terrain
    vec3 worldPos;
    vec3 worldNormal;         // derived from the heightmap, not normalized
    float normalizedHeight;   // raw spline height, 0..1
    float curvature;          // heightmap laplacian, positive concave, negative convex
    uint lod;
    uint continentalnessTex;
    uint erosionTex;
    uint peaksValleysTex;
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

float LinearRGBToLuminance(vec3 rgb) {
    const vec3 LuminanceWeights = vec3(0.2126, 0.7152, 0.0722);
    return dot(rgb, LuminanceWeights);
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

#ifndef TWO_PI
#define TWO_PI 6.28318530718
#endif

// Which horizontal direction the surface faces, as 0..1 around Y where 0 is +Z
float horizontalFacingAngle(vec3 normal) {
    vec3 n = normalize(normal);
    return fract(atan(n.x, n.z) / TWO_PI + 1.0);
}

// Per-axis projection weights, sharpness tightens the transition between the three planes
vec3 triplanarWeights(vec3 normal, float sharpness) {
    vec3 w = pow(abs(normalize(normal)), vec3(max(sharpness, 1.0)));
    return w / max(w.x + w.y + w.z, 1e-5);
}

vec4 triplanarSample(sampler2D tex, vec3 worldPos, vec3 normal, float scale, float sharpness) {
    vec3 w = triplanarWeights(normal, sharpness);
    return texture(tex, worldPos.zy * scale) * w.x
         + texture(tex, worldPos.xz * scale) * w.y
         + texture(tex, worldPos.xy * scale) * w.z;
}

// Triplanar tangent-space normal map, whiteout blended into world space. Needs no tangent frame,
// which is what makes it the only normal mapping terrain can express.
vec3 triplanarNormal(sampler2D tex, vec3 worldPos, vec3 normal, float scale, float sharpness) {
    vec3 n = normalize(normal);
    vec3 w = triplanarWeights(n, sharpness);

    vec3 tx = texture(tex, worldPos.zy * scale).xyz * 2.0 - 1.0;
    vec3 ty = texture(tex, worldPos.xz * scale).xyz * 2.0 - 1.0;
    vec3 tz = texture(tex, worldPos.xy * scale).xyz * 2.0 - 1.0;

    vec3 nx = vec3(tx.xy + n.zy, abs(tx.z) * n.x);
    vec3 ny = vec3(ty.xy + n.xz, abs(ty.z) * n.y);
    vec3 nz = vec3(tz.xy + n.xy, abs(tz.z) * n.z);

    return normalize(nx.zyx * w.x + ny.xzy * w.y + nz.xyz * w.z);
}

// Weight for b when blending two layers by their own height maps rather than lerping linearly.
// sharpness is the width of the band where both layers still contribute.
float heightBlendWeight(float heightA, float heightB, float t, float sharpness) {
    float a = heightA * (1.0 - t);
    float b = heightB * t;
    float m = max(a, b) - max(sharpness, 1e-5);
    float wa = max(a - m, 0.0);
    float wb = max(b - m, 0.0);
    return wb / max(wa + wb, 1e-5);
}

#endif // MATERIAL_COMMON_GLSL

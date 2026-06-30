// Must match CPU TerrainChunkGPUData in TerrainTypes.h
struct TerrainChunkData {
    ivec2 coord;        // Chunk grid coordinate
    float chunkSize;    // World size of chunk edge
    uint lod;           // Current LOD level

    vec2 worldOffset;   // World position of chunk corner
    float minHeight;    // Min Y for AABB culling
    float maxHeight;    // Max Y for AABB culling

    vec4 bounds;        // minX, minZ, maxX, maxZ
    uint neighborLODs;  // Packed neighbor LOD info
    uint flags;         // Visibility flags
    uint _pad[2];       // Padding to 64 bytes
};

// One row per noise category (continentalness, erosion, peaks/valleys)
const float TERRAIN_SPLINE_ROWS = 3.0;

// Evaluate a baked spline curve. t (the noise value in [0,1]) indexes along the
// row; v hits the exact row center so linear filtering never bleeds between rows.
float sampleSplineCurve(sampler2D curveSampler, float t, float row) {
    float v = (row + 0.5) / TERRAIN_SPLINE_ROWS;
    return texture(curveSampler, vec2(clamp(t, 0.0, 1.0), v)).r;
}

float sampleHeightRaw_CEPV(vec2 worldXZ, float terrainWorldSize, sampler2D contSampler, sampler2D erosSampler, sampler2D pvSampler, sampler2D curveSampler) {
    vec2 uv = worldXZ / terrainWorldSize + 0.5;
    float c = texture(contSampler, uv).r;
    float e = texture(erosSampler, uv).r;
    float pv = texture(pvSampler, uv).r;

    float base = sampleSplineCurve(curveSampler, c, 0.0);          // continentalness sets base elevation
    float relief = 1.0 - sampleSplineCurve(curveSampler, e, 1.0);  // erosion flattens relief
    float detail = sampleSplineCurve(curveSampler, pv, 2.0) * 2.0 - 1.0;

    return clamp(base + detail * relief * 0.5, 0.0, 1.0);
}

float sampleHeightRaw_Single(vec2 worldXZ, float terrainWorldSize, sampler2D hmSampler) {
    vec2 uv = worldXZ / terrainWorldSize + 0.5;
    return texture(hmSampler, uv).r;
}

float rawToWorldHeight(float raw, float heightScale) {
    return (raw * 2.0 - 1.0) * heightScale;
}

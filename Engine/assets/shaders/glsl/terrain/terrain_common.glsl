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

vec3 computeLUTCoord(vec2 worldXZ, float terrainWorldSize, sampler2D contSampler, sampler2D erosSampler, sampler2D pvSampler) {
    vec2 uv = worldXZ / terrainWorldSize + 0.5;
    float c = texture(contSampler, uv).r * 2.0 - 1.0;
    float e = texture(erosSampler, uv).r * 2.0 - 1.0;
    float pv = texture(pvSampler, uv).r * 2.0 - 1.0;
    return vec3(c, e, pv) * 0.5 + 0.5;
}

float sampleHeightRaw_CEPV(vec2 worldXZ, float terrainWorldSize, sampler2D contSampler, sampler2D erosSampler, sampler2D pvSampler, sampler3D lutSampler) {
    vec3 lutCoord = computeLUTCoord(worldXZ, terrainWorldSize, contSampler, erosSampler, pvSampler);
    return texture(lutSampler, lutCoord).r;
}

float sampleHeightRaw_Single(vec2 worldXZ, float terrainWorldSize, sampler2D hmSampler) {
    vec2 uv = worldXZ / terrainWorldSize + 0.5;
    return texture(hmSampler, uv).r;
}

float rawToWorldHeight(float raw, float heightScale) {
    return (raw - 0.5) * heightScale;
}

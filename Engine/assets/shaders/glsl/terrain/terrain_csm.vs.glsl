#version 460

#extension GL_EXT_multiview : require
#extension GL_EXT_nonuniform_qualifier : require

// Cascade matrices (same binding as regular CSM)
layout(set = 0, binding = 3) uniform CascadeMatrices {
    mat4 lightViewProjection[4];
} u_cascades[];

// Chunk data (bindless SSBO)
struct TerrainChunkData {
    vec2 worldOffset;
    float chunkSize;
    uint lod;

    vec4 bounds;
    float minHeight;
    float maxHeight;
    uint neighborLODs;
    uint flags;
};

layout(set = 3, binding = 1) readonly buffer TerrainChunkBuffer {
    TerrainChunkData chunks[];
} u_chunks[];

layout(set = 3, binding = 0) uniform sampler2D u_textures[];
layout(set = 3, binding = 0) uniform sampler3D u_textures3D[];

layout(push_constant) uniform TerrainCSMPushConstants {
    uint cascadeMatricesIndex;
    uint chunkDataBufferIndex;
    uint continentalnessIndex;
    uint erosionIndex;
    uint peaksValleysIndex;
    uint noiseLUTIndex;
    uint lodResolution;
    float heightScale;
    float terrainWorldSize;
} pc;

float sampleHeight(vec2 worldXZ) {
    vec2 uv = worldXZ / pc.terrainWorldSize + 0.5;

    float c = texture(u_textures[pc.continentalnessIndex], uv).r * 2.0 - 1.0;
    float e = texture(u_textures[pc.erosionIndex], uv).r * 2.0 - 1.0;
    float pv = texture(u_textures[pc.peaksValleysIndex], uv).r * 2.0 - 1.0;

    vec3 lutCoord = vec3(c, e, pv) * 0.5 + 0.5;
    float raw = texture(u_textures3D[pc.noiseLUTIndex], lutCoord).r;

    return (raw - 0.5) * pc.heightScale;
}

void main() {
    // Get chunk index from instance ID (set by indirect draw command)
    uint chunkIndex = gl_InstanceIndex;
    TerrainChunkData chunk = u_chunks[pc.chunkDataBufferIndex].chunks[chunkIndex];

    // Use resolution from push constants (matches bound index buffer)
    uint resolution = pc.lodResolution;

    // Calculate grid position from vertex index
    uint row = gl_VertexIndex / resolution;
    uint col = gl_VertexIndex % resolution;

    // Calculate local position within chunk (0 to chunkSize)
    float spacing = chunk.chunkSize / float(resolution - 1);
    vec2 localPos = vec2(float(col) * spacing, float(row) * spacing);

    // World position XZ
    vec2 worldXZ = chunk.worldOffset + localPos;

    // Sample heightmap for Y
    float height = sampleHeight(worldXZ);

    vec3 worldPos = vec3(worldXZ.x, height, worldXZ.y);

    // Transform to clip space using cascade matrix for current view
    gl_Position = u_cascades[pc.cascadeMatricesIndex].lightViewProjection[gl_ViewIndex] * vec4(worldPos, 1.0);
}

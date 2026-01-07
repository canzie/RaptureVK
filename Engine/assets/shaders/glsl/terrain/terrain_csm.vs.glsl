#version 460

#extension GL_EXT_multiview : require
#extension GL_EXT_nonuniform_qualifier : require

#include "terrain/terrain_common.glsl"

// Cascade matrices (same binding as regular CSM)
layout(set = 0, binding = 3) uniform CascadeMatrices {
    mat4 lightViewProjection[4];
} u_cascades[];

layout(set = 3, binding = 1) readonly buffer TerrainChunkBuffer {
    TerrainChunkData chunks[];
} u_chunks[];

layout(set = 3, binding = 0) uniform sampler2D u_textures[];
layout(set = 3, binding = 0) uniform sampler3D u_textures3D[];

layout(push_constant) uniform TerrainCSMPushConstants {
    uint cascadeMatricesIndex;
    uint chunkDataBufferIndex;
    uint continentalnessIndex; // Also used for single heightmap when useMultiNoise = 0
    uint erosionIndex;
    uint peaksValleysIndex;
    uint noiseLUTIndex;
    uint useMultiNoise;
    uint lodResolution;
    float heightScale;
    float terrainWorldSize;
} pc;


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

    float raw = pc.useMultiNoise > 0u
        ? sampleHeightRaw_CEPV(worldXZ, pc.terrainWorldSize, u_textures[pc.continentalnessIndex], u_textures[pc.erosionIndex], u_textures[pc.peaksValleysIndex], u_textures3D[pc.noiseLUTIndex])
        : sampleHeightRaw_Single(worldXZ, pc.terrainWorldSize, u_textures[pc.continentalnessIndex]);
    float height = rawToWorldHeight(raw, pc.heightScale);

    vec3 worldPos = vec3(worldXZ.x, height, worldXZ.y);

    // Transform to clip space using cascade matrix for current view
    gl_Position = u_cascades[pc.cascadeMatricesIndex].lightViewProjection[gl_ViewIndex] * vec4(worldPos, 1.0);
}

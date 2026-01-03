#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "terrain/terrain_common.glsl"

// No vertex attributes - position generated from gl_VertexIndex

// Outputs to fragment shader
layout(location = 0) out vec4 outFragPosDepth;
layout(location = 3) out flat uint outChunkIndex;
layout(location = 4) out flat uint outLOD;
layout(location = 5) out float outNormalizedHeight;

// Camera data (bindless)
layout(set = 0, binding = 0) uniform CameraDataBuffer {
    mat4 view;
    mat4 proj;
} u_camera[];

layout(set = 3, binding = 1) readonly buffer TerrainChunkBuffer {
    TerrainChunkData chunks[];
} u_chunks[];

// Textures (bindless)
layout(set = 3, binding = 0) uniform sampler2D u_textures[];
layout(set = 3, binding = 0) uniform sampler3D u_textures3D[];

layout(push_constant) uniform TerrainPushConstants {
    uint cameraBindlessIndex;
    uint chunkDataBufferIndex;
    uint continentalnessIndex; // Also used for single heightmap when useMultiNoise = 0
    uint erosionIndex;
    uint peaksValleysIndex;
    uint noiseLUTIndex;
    uint useMultiNoise;
    uint lodResolution;
    float heightScale;
    float terrainWorldSize;
    uint grassMaterialIndex;
    uint rockMaterialIndex;
    uint snowMaterialIndex;
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

    float rawHeight = pc.useMultiNoise > 0u
        ? sampleHeightRaw_CEPV(worldXZ, pc.terrainWorldSize, u_textures[pc.continentalnessIndex], u_textures[pc.erosionIndex], u_textures[pc.peaksValleysIndex], u_textures3D[pc.noiseLUTIndex])
        : sampleHeightRaw_Single(worldXZ, pc.terrainWorldSize, u_textures[pc.continentalnessIndex]);
    float height = rawToWorldHeight(rawHeight, pc.heightScale);

    vec3 worldPos = vec3(worldXZ.x, height, worldXZ.y);

    // Transform to clip space
    mat4 view = u_camera[pc.cameraBindlessIndex].view;
    mat4 proj = u_camera[pc.cameraBindlessIndex].proj;
    vec4 viewPos = view * vec4(worldPos, 1.0);
    gl_Position = proj * viewPos;

    // Outputs
    outFragPosDepth = vec4(worldPos, -viewPos.z);
    outChunkIndex = chunkIndex;
    outLOD = chunk.lod;
    outNormalizedHeight = rawHeight;
}

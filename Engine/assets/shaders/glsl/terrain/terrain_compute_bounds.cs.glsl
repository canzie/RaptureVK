#version 460

#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "terrain/terrain_common.glsl"

layout(set = 3, binding = 1) buffer ChunkDataBuffer {
    TerrainChunkData chunks[];
} u_chunkData[];

layout(set = 3, binding = 0) uniform sampler2D u_textures[];
layout(set = 3, binding = 0) uniform sampler3D u_textures3D[];

layout(push_constant) uniform PushConstants {
    uint chunkDataBufferIndex;
    uint continentalnessIndex; // Also used for single heightmap when useMultiNoise = 0
    uint erosionIndex;
    uint peaksValleysIndex;
    uint noiseLUTIndex;
    uint useMultiNoise;
    float heightScale;
    float terrainWorldSize;
    float chunkSize;
    vec2 cameraPos;
    int loadRadius;
    uint sampleResolution;
} pc;


void main() {
    uint chunkIdx = gl_GlobalInvocationID.x;

    // Grid is (2*loadRadius+1) x (2*loadRadius+1)
    int gridSize = 2 * pc.loadRadius + 1;
    uint totalChunks = uint(gridSize * gridSize);

    if (chunkIdx >= totalChunks) {
        return;
    }

    // Calculate chunk coord from linear index
    int localX = int(chunkIdx) % gridSize - pc.loadRadius;
    int localZ = int(chunkIdx) / gridSize - pc.loadRadius;

    // Camera chunk coord
    ivec2 cameraChunk = ivec2(floor(pc.cameraPos / pc.chunkSize));
    ivec2 coord = cameraChunk + ivec2(localX, localZ);

    // Compute world offset from coord
    vec2 worldOffset = vec2(float(coord.x) - 0.5, float(coord.y) - 0.5) * pc.chunkSize;

    // Sample heights across the chunk to find min/max
    float minHeight = 1e10;
    float maxHeight = -1e10;

    float step = pc.chunkSize / float(pc.sampleResolution - 1);

    for (uint row = 0; row < pc.sampleResolution; row++) {
        for (uint col = 0; col < pc.sampleResolution; col++) {
            vec2 localPos = vec2(float(col) * step, float(row) * step);
            vec2 worldXZ = worldOffset + localPos;

            float rawHeight = pc.useMultiNoise > 0u
                ? sampleHeightRaw_CEPV(worldXZ, pc.terrainWorldSize, u_textures[pc.continentalnessIndex], u_textures[pc.erosionIndex], u_textures[pc.peaksValleysIndex], u_textures3D[pc.noiseLUTIndex])
                : sampleHeightRaw_Single(worldXZ, pc.terrainWorldSize, u_textures[pc.continentalnessIndex]);
            float height = rawToWorldHeight(rawHeight, pc.heightScale);

            minHeight = min(minHeight, height);
            maxHeight = max(maxHeight, height);
        }
    }

    // Write all chunk data
    TerrainChunkData data;
    data.coord = coord;
    data.chunkSize = pc.chunkSize;
    data.lod = 0; // TODO: compute LOD based on distance to camera
    data.worldOffset = worldOffset;
    data.minHeight = minHeight;
    data.maxHeight = maxHeight;
    data.bounds = vec4(worldOffset.x, worldOffset.y, worldOffset.x + pc.chunkSize, worldOffset.y + pc.chunkSize);
    data.neighborLODs = 0;
    data.flags = 1;

    u_chunkData[pc.chunkDataBufferIndex].chunks[chunkIdx] = data;
}

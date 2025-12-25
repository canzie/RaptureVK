#version 460

#extension GL_EXT_nonuniform_qualifier : require

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

// Chunk data (bindless SSBO - set 3, binding 1)
struct TerrainChunkData {
    vec2 worldOffset;
    float chunkSize;
    uint lod;

    vec4 bounds;        // minX, minZ, maxX, maxZ
    float minHeight;
    float maxHeight;
    uint neighborLODs;
    uint flags;
};

layout(set = 3, binding = 1) readonly buffer TerrainChunkBuffer {
    TerrainChunkData chunks[];
} u_chunks[];

// Heightmap texture (bindless)
layout(set = 3, binding = 0) uniform sampler2D u_textures[];

// Push constants
layout(push_constant) uniform TerrainPushConstants {
    uint cameraBindlessIndex;
    uint chunkDataBufferIndex;
    uint heightmapIndex;
    uint tileMaterialIndex;
    uint lodResolution;     // Vertices per edge for current LOD
    float heightScale;
    float terrainWorldSize;
} pc;

// LOD resolutions lookup (must match CPU)
const uint LOD_RESOLUTIONS[4] = uint[4](129, 65, 33, 17);

// Sample raw heightmap value [0,1]
float sampleHeightmapRaw(vec2 worldXZ) {
    vec2 uv = worldXZ / pc.terrainWorldSize + 0.5;
    return texture(u_textures[pc.heightmapIndex], uv).r;
}

// Convert raw heightmap to world height (centered around 0)
float rawToWorldHeight(float raw) {
    return (raw - 0.5) * pc.heightScale;
}

// Sample height from heightmap (centered around 0)
float sampleHeight(vec2 worldXZ) {
    return rawToWorldHeight(sampleHeightmapRaw(worldXZ));
}

void main() {
    // Get chunk index from instance ID (set by indirect draw command)
    uint chunkIndex = gl_InstanceIndex;
    TerrainChunkData chunk = u_chunks[pc.chunkDataBufferIndex].chunks[chunkIndex];

    // Get resolution for this chunk's LOD
    uint resolution = LOD_RESOLUTIONS[chunk.lod];

    // Calculate grid position from vertex index
    uint row = gl_VertexIndex / resolution;
    uint col = gl_VertexIndex % resolution;

    // Calculate local position within chunk (0 to chunkSize)
    float spacing = chunk.chunkSize / float(resolution - 1);
    vec2 localPos = vec2(float(col) * spacing, float(row) * spacing);

    // World position XZ
    vec2 worldXZ = chunk.worldOffset + localPos;

    // Sample heightmap for Y
    float rawHeight = sampleHeightmapRaw(worldXZ);
    float height = rawToWorldHeight(rawHeight);

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

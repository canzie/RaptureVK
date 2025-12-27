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

// Textures (bindless)
layout(set = 3, binding = 0) uniform sampler2D u_textures[];
layout(set = 3, binding = 0) uniform sampler3D u_textures3D[];

layout(push_constant) uniform TerrainPushConstants {
    uint cameraBindlessIndex;
    uint chunkDataBufferIndex;
    uint continentalnessIndex;
    uint erosionIndex;
    uint peaksValleysIndex;
    uint noiseLUTIndex;
    uint lodResolution;
    float heightScale;
    float terrainWorldSize;
} pc;

float sampleMultiNoise(vec2 worldXZ) {
    vec2 uv = worldXZ / pc.terrainWorldSize + 0.5;

    float c = texture(u_textures[pc.continentalnessIndex], uv).r * 2.0 - 1.0;
    float e = texture(u_textures[pc.erosionIndex], uv).r * 2.0 - 1.0;
    float pv = texture(u_textures[pc.peaksValleysIndex], uv).r * 2.0 - 1.0;

    vec3 lutCoord = vec3(c, e, pv) * 0.5 + 0.5;
    return texture(u_textures3D[pc.noiseLUTIndex], lutCoord).r;
}

float rawToWorldHeight(float raw) {
    return (raw - 0.5) * pc.heightScale;
}

float sampleHeight(vec2 worldXZ) {
    return rawToWorldHeight(sampleMultiNoise(worldXZ));
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

    float rawHeight = sampleMultiNoise(worldXZ);
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

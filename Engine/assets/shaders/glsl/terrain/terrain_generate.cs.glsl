#version 460

#extension GL_EXT_nonuniform_qualifier : require


// Generates terrain chunk geometry from heightmap
// One workgroup per row of vertices, threads generate individual vertices

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Push constants
layout(push_constant) uniform PushConstants {
    vec2 chunkWorldOffset;  // World position of chunk corner
    float chunkWorldSize;   // World size of chunk edge
    float heightScale;      // Height multiplier
    uint resolution;        // Vertices per edge (e.g., 33 for 32 quads)
    uint vertexOffset;      // Offset into vertex buffer
    uint indexOffset;       // Offset into index buffer
    float terrainWorldSize; // Total terrain world size (for UV mapping to heightmap)
    uint heightmapHandle;    // Index into bindless texture array
} pc;

// Heightmap input
layout(set = 3, binding = 0) uniform sampler2D gTextures[];

// Output buffers
struct TerrainVertex {
    vec3 position;
    float pad0;
    vec3 normal;
    float pad1;
    vec2 uv;
    vec2 pad2;
};

layout(std430, set = 4, binding = 1) buffer VertexBuffer {
    TerrainVertex vertices[];
};

layout(std430, set = 4, binding = 2) buffer IndexBuffer {
    uint indices[];
};

// Sample height from heightmap
float sampleHeight(vec2 worldXZ) {
    vec2 uv = worldXZ / pc.terrainWorldSize + 0.5;
    return texture(gTextures[pc.heightmapHandle], uv).r * pc.heightScale;
}

// Calculate normal from heightmap gradient
vec3 calculateNormal(vec2 worldXZ, float delta) {
    float hL = sampleHeight(worldXZ + vec2(-delta, 0.0));
    float hR = sampleHeight(worldXZ + vec2(delta, 0.0));
    float hD = sampleHeight(worldXZ + vec2(0.0, -delta));
    float hU = sampleHeight(worldXZ + vec2(0.0, delta));
    return normalize(vec3(hL - hR, 2.0 * delta, hD - hU));
}

void main() {
    uint row = gl_WorkGroupID.x;
    uint col = gl_LocalInvocationID.x;

    // Skip if outside resolution
    if (row >= pc.resolution || col >= pc.resolution) {
        return;
    }

    uint vertexIndex = row * pc.resolution + col;
    float spacing = pc.chunkWorldSize / float(pc.resolution - 1);

    // Calculate world position
    float worldX = pc.chunkWorldOffset.x + float(col) * spacing;
    float worldZ = pc.chunkWorldOffset.y + float(row) * spacing;
    vec2 worldXZ = vec2(worldX, worldZ);

    float height = sampleHeight(worldXZ);

    // Write vertex
    uint outIndex = pc.vertexOffset + vertexIndex;
    vertices[outIndex].position = vec3(worldX, height, worldZ);
    vertices[outIndex].normal = calculateNormal(worldXZ, spacing);
    vertices[outIndex].uv = vec2(float(col) / float(pc.resolution - 1),
                                  float(row) / float(pc.resolution - 1));

    // Generate indices (only for non-edge vertices)
    if (row < pc.resolution - 1 && col < pc.resolution - 1) {
        uint quadIndex = row * (pc.resolution - 1) + col;
        uint baseIndex = pc.indexOffset + quadIndex * 6;

        uint topLeft = vertexIndex;
        uint topRight = topLeft + 1;
        uint bottomLeft = topLeft + pc.resolution;
        uint bottomRight = bottomLeft + 1;

        // Triangle 1
        indices[baseIndex + 0] = pc.vertexOffset + topLeft;
        indices[baseIndex + 1] = pc.vertexOffset + bottomLeft;
        indices[baseIndex + 2] = pc.vertexOffset + bottomRight;

        // Triangle 2
        indices[baseIndex + 3] = pc.vertexOffset + topLeft;
        indices[baseIndex + 4] = pc.vertexOffset + bottomRight;
        indices[baseIndex + 5] = pc.vertexOffset + topRight;
    }
}

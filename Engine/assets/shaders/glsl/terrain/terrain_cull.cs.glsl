#version 460

#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "terrain_common.glsl"

// VkDrawIndexedIndirectCommand structure
struct DrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

// Frustum planes buffer (6 planes, each vec4(normal.xyz, distance))
// Can be from main camera, shadow cascade, spot light, etc.
layout(set = 3, binding = 1) readonly buffer FrustumPlanesBuffer {
    vec4 planes[]; // 6 planes per frustum
} u_frustumPlanes[];

// Input: chunk data
layout(set = 3, binding = 1) readonly buffer ChunkDataBuffer {
    TerrainChunkData chunks[];
} u_chunkData[];

// Output: indirect draw commands (grouped by LOD)
layout(set = 3, binding = 1) buffer IndirectCommandBuffer {
    DrawIndexedIndirectCommand commands[];
} u_indirectCommands[];

// Output: draw count per LOD (atomic counters)
layout(set = 3, binding = 1) buffer DrawCountBuffer {
    uint counts[];
} u_drawCounts[];

// Push constants - designed for flexibility
layout(push_constant) uniform CullPushConstants {
    // Culling origin (camera pos for LOD, light pos for range culling)
    vec3 cullOrigin;
    uint chunkCount;

    // Terrain parameters
    float heightScale;
    // Range culling: 0 = disabled, >0 = max distance from cullOrigin
    float cullRange;
    // LOD mode: 0 = use LOD based on distance, 1 = force specific LOD (for shadows)
    uint lodMode;
    uint forcedLOD;

    // Buffer indices (bindless)
    uint frustumPlanesBufferIndex;
    uint chunkDataBufferIndex;
    uint drawCountBufferIndex;
    uint _pad0;

    // Per-LOD indirect buffer indices
    uint indirectBufferIndices[4];
} pc;

// LOD constants (must match CPU)
const uint LOD_COUNT = 4;
const uint LOD_INDEX_COUNTS[4] = uint[4](
    (128 * 128 * 6),  // LOD0: 129 vertices = 128 quads = 128*128*6 indices
    (64 * 64 * 6),    // LOD1
    (32 * 32 * 6),    // LOD2
    (16 * 16 * 6)     // LOD3
);
const float LOD_DISTANCES[4] = float[4](128.0, 256.0, 512.0, 1024.0);

// Test AABB against frustum planes
// Returns true if AABB is visible (inside or intersecting frustum)
bool isAABBVisible(vec3 aabbMin, vec3 aabbMax, uint frustumIndex) {
    for (int i = 0; i < 6; i++) {
        vec4 plane = u_frustumPlanes[pc.frustumPlanesBufferIndex].planes[frustumIndex * 6 + i];

        // Find the positive vertex (furthest along plane normal)
        vec3 p = aabbMin;
        if (plane.x >= 0.0) p.x = aabbMax.x;
        if (plane.y >= 0.0) p.y = aabbMax.y;
        if (plane.z >= 0.0) p.z = aabbMax.z;

        // If positive vertex is behind plane, AABB is fully outside
        if (dot(plane.xyz, p) + plane.w < 0.0) {
            return false;
        }
    }
    return true;
}

// Test AABB against sphere (for range culling)
bool isAABBInRange(vec3 aabbMin, vec3 aabbMax, vec3 center, float radius) {
    // Find closest point on AABB to sphere center
    vec3 closest = clamp(center, aabbMin, aabbMax);
    float distSq = dot(closest - center, closest - center);
    return distSq <= radius * radius;
}

// Calculate LOD based on distance
uint calculateLOD(float distance) {
    for (uint lod = 0; lod < LOD_COUNT; lod++) {
        if (distance < LOD_DISTANCES[lod]) {
            return lod;
        }
    }
    return LOD_COUNT - 1;
}

void main() {
    uint chunkIdx = gl_GlobalInvocationID.x;
    if (chunkIdx >= pc.chunkCount) {
        return;
    }

    // Load chunk data
    TerrainChunkData chunk = u_chunkData[pc.chunkDataBufferIndex].chunks[chunkIdx];

    // Skip if chunk is marked as inactive
    if ((chunk.flags & 1u) == 0u) {
        return;
    }

    vec3 aabbMin = vec3(
        chunk.worldOffset.x,
        chunk.minHeight,
        chunk.worldOffset.y
    );
    vec3 aabbMax = vec3(
        chunk.worldOffset.x + chunk.chunkSize,
        chunk.maxHeight,
        chunk.worldOffset.y + chunk.chunkSize
    );

    // Range culling (for spot/point lights)
    if (pc.cullRange > 0.0) {
        if (!isAABBInRange(aabbMin, aabbMax, pc.cullOrigin, pc.cullRange)) {
            return;
        }
    }

    // Frustum culling
    if (!isAABBVisible(aabbMin, aabbMax, 0)) {
        return;
    }

    // Determine LOD
    uint lod;
    if (pc.lodMode == 1) {
        // Forced LOD (typically for shadow maps - use lower detail)
        lod = pc.forcedLOD;
    } else {
        // Distance-based LOD
        vec2 chunkCenter = chunk.worldOffset + vec2(chunk.chunkSize * 0.5);
        float distance = length(vec2(pc.cullOrigin.x, pc.cullOrigin.z) - chunkCenter);
        lod = calculateLOD(distance);
    }

    // Atomically get slot in output buffer for this LOD
    uint slotInLOD = atomicAdd(u_drawCounts[pc.drawCountBufferIndex].counts[lod], 1);

    // Write indirect command to the LOD's buffer
    DrawIndexedIndirectCommand cmd;
    cmd.indexCount = LOD_INDEX_COUNTS[lod];
    cmd.instanceCount = 1;
    cmd.firstIndex = 0;
    cmd.vertexOffset = 0;
    cmd.firstInstance = chunkIdx;

    u_indirectCommands[pc.indirectBufferIndices[lod]].commands[slotInLOD] = cmd;
}

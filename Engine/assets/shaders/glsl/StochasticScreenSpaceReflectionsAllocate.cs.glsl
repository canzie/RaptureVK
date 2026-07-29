#version 460

#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

// One entry per ray a tile was allocated, so the trace dispatches exactly the work that exists
layout(std430, set = 4, binding = 0) writeonly buffer WorkItems {
    uvec2 items[];
} u_workItems;

// A VkDispatchIndirectCommand. x doubles as the allocation counter, so the atomic that reserves
// slots also produces the group count and no separate reduction is needed.
layout(std430, set = 4, binding = 1) buffer IndirectArgs {
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
} u_indirectArgs;

layout(push_constant) uniform PushConstants {
    uint tileRayCountTextureIndex;
    uint maxItems;
    ivec2 tileCount;
} pc;

void main() {
    ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(tile, pc.tileCount))) {
        return;
    }

    int rayCount = int(texelFetch(gTextures[nonuniformEXT(pc.tileRayCountTextureIndex)], tile, 0).r);
    if (rayCount <= 0) {
        return;
    }

    uint base = atomicAdd(u_indirectArgs.groupCountX, uint(rayCount));

    // The buffer is sized for every tile at the maximum budget, so this only trips if the tile grid
    // and the buffer have gone out of step
    if (base + uint(rayCount) > pc.maxItems) {
        return;
    }

    uint tileIndex = uint(tile.y) * uint(pc.tileCount.x) + uint(tile.x);
    for (int ray = 0; ray < rayCount; ++ray) {
        u_workItems.items[base + uint(ray)] = uvec2(tileIndex, uint(ray));
    }
}

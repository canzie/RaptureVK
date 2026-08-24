#version 450

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in flat uint inEntityId;

// The counter is incremented for every fragment, so a count past maxLayers reports the overflow
layout(set = 3, binding = 1) buffer PickCountBuffer {
    uint counts[];
} u_pickCounts[];

layout(set = 3, binding = 1) buffer PickEntryBuffer {
    uvec4 entries[];
} u_pickEntries[];

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    uint batchInfoBufferIndex;
    uint meshSSBOIndex;
    uint countBufferIndex;
    uint entryBufferIndex;
    uint regionWidth;
    uint regionHeight;
    uint maxLayers;
} pc;

void main() {
    uvec2 coord = uvec2(gl_FragCoord.xy);
    if (coord.x >= pc.regionWidth || coord.y >= pc.regionHeight) {
        return;
    }

    uint pixel = coord.y * pc.regionWidth + coord.x;
    uint slot = atomicAdd(u_pickCounts[pc.countBufferIndex].counts[pixel], 1u);

    if (slot < pc.maxLayers) {
        // geometry the renderer drew answers with its entity, that being the identity it has
        u_pickEntries[pc.entryBufferIndex].entries[pixel * pc.maxLayers + slot] =
            uvec4(inEntityId, 0u, floatBitsToUint(gl_FragCoord.z), 0u);
    }
}

#version 450

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 aPosition;

layout(location = 0) out flat uint outEntityId;

struct MeshGPUData {
    mat4 model;
    uint materialIndex;
    uint flags;
    uint entityId;
    uint skinOffsets;
};

struct ObjectInfo {
    uint meshIndex;
    uint materialIndex;
};

layout(set = 0, binding = 6) readonly buffer BatchInfoBuffer {
    ObjectInfo objects[];
} u_batchInfo[];

layout(set = 2, binding = 0) readonly buffer MeshDataSSBO {
    MeshGPUData meshes[];
} u_meshSSBO[];

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
    uint meshSlotIndex = u_batchInfo[pc.batchInfoBufferIndex].objects[gl_InstanceIndex].meshIndex;

    mat4 model = u_meshSSBO[pc.meshSSBOIndex].meshes[meshSlotIndex].model;
    outEntityId = u_meshSSBO[pc.meshSSBOIndex].meshes[meshSlotIndex].entityId;

    gl_Position = pc.viewProj * model * vec4(aPosition, 1.0);
}

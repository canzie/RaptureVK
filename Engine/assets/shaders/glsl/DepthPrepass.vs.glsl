#version 450

#extension GL_EXT_nonuniform_qualifier : require

#include "common/CameraCommon.glsl"

#ifdef IS_SKINNED_MESH
#include "common/Skinning.glsl"
#endif // IS_SKINNED_MESH

layout(location = 0) in vec3 aPosition;

#ifdef IS_SKINNED_MESH
layout(location = 5) in vec4 aWeights;
layout(location = 6) in uvec4 aJoints;
#endif // IS_SKINNED_MESH

struct MeshGPUData {
    mat4 model;
    uint materialIndex;
    uint flags;
    uint entityId;
    uint boneOffset;
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
    uint batchInfoBufferIndex;
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint meshSSBOIndex;
    uint skeletonSSBOIndex;
} pc;

// The g-buffer compares equal against what this writes, so the transform has to be applied in the
// same order it uses rather than by concatenating the matrices first
void main() {
    uint meshSlotIndex = u_batchInfo[pc.batchInfoBufferIndex].objects[gl_InstanceIndex].meshIndex;

    mat4 model = u_meshSSBO[pc.meshSSBOIndex].meshes[meshSlotIndex].model;

#ifdef IS_SKINNED_MESH
    uint boneOffset = u_meshSSBO[pc.meshSSBOIndex].meshes[meshSlotIndex].boneOffset;
    if (boneOffset != NO_BONE_OFFSET) {
        model = model * Skinning_blendMatrix(pc.skeletonSSBOIndex, boneOffset, aJoints, aWeights);
    }
#endif // IS_SKINNED_MESH

    vec3 worldPosition = vec3(model * vec4(aPosition, 1.0));
    vec4 viewPosition = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex].view * vec4(worldPosition, 1.0);

    gl_Position = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex].proj * viewPosition;
}

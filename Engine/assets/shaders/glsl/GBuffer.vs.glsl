#version 450

#extension GL_EXT_nonuniform_qualifier : require

#include "common/CameraCommon.glsl"

#ifdef IS_SKINNED_MESH
#include "common/Skinning.glsl"
#endif // IS_SKINNED_MESH

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

#ifdef IS_SKINNED_MESH
layout(location = 5) in vec4 aWeights;
layout(location = 6) in uvec4 aJoints;
#endif // IS_SKINNED_MESH

layout(location = 0) out vec4 outFragPosDepth;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec3 outBitangent;
layout(location = 5) out flat uint outFlags;
layout(location = 6) out flat uint outMaterialIndex;

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

// Bit flag definitions
const uint FLAG_HAS_NORMALS = 1u;
const uint FLAG_HAS_TANGENTS = 2u;
const uint FLAG_HAS_BITANGENTS = 4u;
const uint FLAG_HAS_TEXCOORDS = 8u;

void main() {

    // Get batch info for this draw call using gl_InstanceIndex
    uint meshSlotIndex = u_batchInfo[pc.batchInfoBufferIndex].objects[gl_InstanceIndex].meshIndex;

    outMaterialIndex = u_batchInfo[pc.batchInfoBufferIndex].objects[gl_InstanceIndex].materialIndex;

    mat4 model = u_meshSSBO[pc.meshSSBOIndex].meshes[meshSlotIndex].model;
    uint flags = u_meshSSBO[pc.meshSSBOIndex].meshes[meshSlotIndex].flags;

#ifdef IS_SKINNED_MESH
    // folded into the model matrix so every basis derived from it below is deformed with the position
    uint boneOffset = u_meshSSBO[pc.meshSSBOIndex].meshes[meshSlotIndex].boneOffset;
    if (boneOffset != NO_BONE_OFFSET) {
        model = model * Skinning_blendMatrix(pc.skeletonSSBOIndex, boneOffset, aJoints, aWeights);
    }
#endif // IS_SKINNED_MESH

    // Use flags to determine attribute availability
    bool hasNormals = (flags & FLAG_HAS_NORMALS) != 0u;
    bool hasTangents = (flags & FLAG_HAS_TANGENTS) != 0u;
    bool hasBitangents = (flags & FLAG_HAS_BITANGENTS) != 0u;
    bool hasTexcoords = (flags & FLAG_HAS_TEXCOORDS) != 0u;

    // Transform to world space
    outFragPosDepth.xyz = vec3(model * vec4(aPosition, 1.0));

    outNormal = vec3(0.0, 1.0, 0.0);
    if (hasNormals) {
        outNormal = normalize(mat3(model) * aNormal);
    }

    outTangent = vec3(1.0, 0.0, 0.0);
    outBitangent = vec3(0.0, 0.0, 1.0);
    if (hasTangents) {
        outTangent = normalize(mat3(model) * aTangent.xyz);
    }

    if (hasNormals && hasTangents) {
        // Re-orthogonalize tangent with respect to normal
        outTangent = normalize(outTangent - dot(outTangent, outNormal) * outNormal);

        if (hasBitangents) {
            // tangent.w is the bitangent handedness, so rebuild the bitangent orthogonal to the
            // frame while preserving it
            vec3 transformedBitangent = normalize(mat3(model) * (cross(aNormal, aTangent.xyz) * aTangent.w));
            vec3 orthogonalBitangent = cross(outNormal, outTangent);
            outBitangent = orthogonalBitangent * sign(dot(transformedBitangent, orthogonalBitangent));
        }
    }

    outTexCoord = vec2(0.0, 0.0);
    if (hasTexcoords) {
        outTexCoord = aTexCoord;
    }

    // Pass flags to fragment shader
    outFlags = flags;

    // Calculate position in view space
    vec4 viewPos = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex].view * vec4(outFragPosDepth.xyz, 1.0);

    // Store linear view-space depth (positive = into screen)
    // For visible objects in front of camera: viewPos.z < 0, so -viewPos.z > 0
    outFragPosDepth.w = -viewPos.z;

    // Final clip space position
    gl_Position = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex].proj * viewPos;
}

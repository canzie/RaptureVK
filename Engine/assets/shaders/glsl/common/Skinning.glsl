#ifndef RAPTURE_SKINNING_GLSL
#define RAPTURE_SKINNING_GLSL

#include "BindingsCommon.glsl"

layout(set = 2, binding = S2_B1_SKELETON_MATRICES_SSBO) readonly buffer SkeletonMatricesSSBO {
    mat4 matrices[];
} u_skeletonSSBO[];

// a mesh drawn against no pose has no bones reserved for it, and its vertices are already its bind pose
const uint NO_BONE_OFFSET = 0xFFFFFFFFu;

/**
 * The skinning matrix a vertex is deformed by, the weighted blend of the joints influencing it.
 *
 * _boneOffset is where this pose's joints start in the arena, so joint j of the pose is
 * matrices[_boneOffset + j].
 */
mat4 Skinning_blendMatrix(uint _skeletonSSBOIndex, uint _boneOffset, uvec4 _joints, vec4 _weights)
{
    return _weights.x * u_skeletonSSBO[_skeletonSSBOIndex].matrices[_boneOffset + _joints.x] +
           _weights.y * u_skeletonSSBO[_skeletonSSBOIndex].matrices[_boneOffset + _joints.y] +
           _weights.z * u_skeletonSSBO[_skeletonSSBOIndex].matrices[_boneOffset + _joints.z] +
           _weights.w * u_skeletonSSBO[_skeletonSSBOIndex].matrices[_boneOffset + _joints.w];
}

#endif // RAPTURE_SKINNING_GLSL

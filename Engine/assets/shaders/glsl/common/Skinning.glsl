#ifndef RAPTURE_SKINNING_GLSL
#define RAPTURE_SKINNING_GLSL

#include "BindingsCommon.glsl"

layout(set = 2, binding = S2_B1_SKELETON_MATRICES_SSBO) readonly buffer SkeletonMatricesSSBO {
    mat4 matrices[];
} u_skeletonSSBO[];

// a mesh drawn against no pose has no joints reserved for it, and its vertices are already its bind pose
const uint NO_SKIN_OFFSET = 0xFFFFu;

/**
 * Whether a mesh row names a pose to deform against.
 */
bool Skinning_isSkinned(uint _skinOffsets)
{
    return (_skinOffsets & NO_SKIN_OFFSET) != NO_SKIN_OFFSET;
}

/**
 * The skinning matrix a vertex is deformed by, the weighted blend of the joints influencing it.
 *
 * A joint contributes where it now stands relative to the pose, undoing where it stood when the
 * mesh was bound to it. Both blocks sit in the same arena: _skinOffsets holds the pose's first
 * joint in its low half and the mesh's first inverse bind in its high half.
 */
mat4 Skinning_blendMatrix(uint _skeletonSSBOIndex, uint _skinOffsets, uvec4 _joints, vec4 _weights)
{
    uint joints = _skinOffsets & NO_SKIN_OFFSET;
    uint binds = _skinOffsets >> 16;

    return _weights.x * (u_skeletonSSBO[_skeletonSSBOIndex].matrices[joints + _joints.x] *
                         u_skeletonSSBO[_skeletonSSBOIndex].matrices[binds + _joints.x]) +
           _weights.y * (u_skeletonSSBO[_skeletonSSBOIndex].matrices[joints + _joints.y] *
                         u_skeletonSSBO[_skeletonSSBOIndex].matrices[binds + _joints.y]) +
           _weights.z * (u_skeletonSSBO[_skeletonSSBOIndex].matrices[joints + _joints.z] *
                         u_skeletonSSBO[_skeletonSSBOIndex].matrices[binds + _joints.z]) +
           _weights.w * (u_skeletonSSBO[_skeletonSSBOIndex].matrices[joints + _joints.w] *
                         u_skeletonSSBO[_skeletonSSBOIndex].matrices[binds + _joints.w]);
}

#endif // RAPTURE_SKINNING_GLSL

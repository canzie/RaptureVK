#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "common/CameraCommon.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, r32f) uniform restrict writeonly image2D outputImage;

layout(push_constant) uniform PushConstants {
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint depthTextureIndex;
    ivec2 outputSize;
} pc;

void main() {
    ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(dst, pc.outputSize))) {
        return;
    }

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];

    float depth = texelFetch(gTextures[nonuniformEXT(pc.depthTextureIndex)], dst, 0).r;
    imageStore(outputImage, dst, vec4(cameraLinearDepth(cam, depth), 0.0, 0.0, 0.0));
}

#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "common/CameraCommon.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, rgba16f) uniform restrict writeonly image2D outDenoised;

layout(push_constant) uniform PushConstants {
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint occlusionTextureIndex;
    uint previousDenoisedTextureIndex;
    uint depthTextureIndex;
    uint normalTextureIndex;
    uint linearDepthTextureIndex;
    uint historyLinearDepthTextureIndex;
    uint hasHistory;
    float depthRejection;
    float hysteresis;
    // Last, so the block ends on the 8 byte alignment the ivec2 gives it and its size matches
    // sizeof on the matching struct
    ivec2 outputSize;
} pc;

void main() {
    ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(dst, pc.outputSize))) {
        return;
    }

    float depth = texelFetch(gTextures[nonuniformEXT(pc.depthTextureIndex)], dst, 0).r;
    if (depth >= 1.0) {
        imageStore(outDenoised, dst, vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }

    float centreDepth = texelFetch(gTextures[nonuniformEXT(pc.linearDepthTextureIndex)], dst, 0).r;

    // Neighbouring pixels traced different slice directions, so averaging over them is not blurring
    // an estimate, it is finishing one. Weighting by depth keeps the average from reaching across a
    // silhouette, where the neighbours describe a different surface rather than the same one.
    vec4 sum = vec4(0.0);
    float weightSum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 coord = clamp(dst + ivec2(x, y), ivec2(0), pc.outputSize - 1);
            float neighbourDepth = texelFetch(gTextures[nonuniformEXT(pc.linearDepthTextureIndex)], coord, 0).r;

            // Relative to the distance itself, so one tolerance holds across the whole depth range
            float weight = max(0.0, 1.0 - abs(neighbourDepth - centreDepth) / (centreDepth * pc.depthRejection));

            sum += texelFetch(gTextures[nonuniformEXT(pc.occlusionTextureIndex)], coord, 0) * weight;
            weightSum += weight;
        }
    }

    vec4 current = sum / max(weightSum, 1e-5);

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];

    vec2 uv = (vec2(dst) + 0.5) / vec2(pc.outputSize);
    vec4 worldH = cam.invViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec3 worldPos = worldH.xyz / worldH.w;

    // Occlusion belongs to the surface rather than to anything seen through it, so unlike a
    // reflection it travels with the surface's own motion vector
    vec2 motion = texelFetch(gTextures[nonuniformEXT(pc.normalTextureIndex)], dst, 0).zw;
    vec2 previousUV = uv - motion;

    // A perspective clip w is the distance in front of the camera, so reprojecting this pixel's
    // world position through the previous view gives the depth the history is only valid at. Where
    // the two disagree the reprojection landed on something else and the accumulation is dropped.
    //
    // The expected depth is camera motion only, so a surface that moved under a still camera fails
    // this and restarts. That is the safe direction to be wrong in: it costs noise on movers rather
    // than dragging their occlusion across the floor behind them.
    vec4 previousClip = cam.prevViewProj * vec4(worldPos, 1.0);
    float expectedDepth = previousClip.w;
    float storedDepth = textureLod(gTextures[nonuniformEXT(pc.historyLinearDepthTextureIndex)], previousUV, 0.0).r;

    bool reprojected = pc.hasHistory != 0u && expectedDepth > 0.0 &&
                       all(greaterThanEqual(previousUV, vec2(0.0))) && all(lessThanEqual(previousUV, vec2(1.0))) &&
                       abs(storedDepth - expectedDepth) <= expectedDepth * pc.depthRejection;

    // No neighbourhood clamp here. Occlusion is already low frequency and spatially filtered by the
    // loop above, so a variance bound would mostly put back the noise this pass just removed, and
    // the failure it would guard against is a disocclusion the depth test above already catches.
    vec4 result = current;
    if (reprojected) {
        vec4 history = textureLod(gTextures[nonuniformEXT(pc.previousDenoisedTextureIndex)], previousUV, 0.0);
        result = mix(current, history, pc.hysteresis);
    }

    // Both filters average directions, which shortens them. Renormalising is what makes the average
    // a direction again rather than letting it drift towards zero over a long history.
    float bentNormalLength = length(result.rgb);
    if (bentNormalLength > 1e-5) {
        result.rgb /= bentNormalLength;
    }

    imageStore(outDenoised, dst, result);
}

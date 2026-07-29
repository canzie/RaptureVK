#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "common/CameraCommon.glsl"
#include "common/ImportanceSampling.glsl"
#include "common/Octahedral.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, rgba16f) uniform restrict writeonly image2D outResolved;

layout(push_constant) uniform PushConstants {
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint hitTextureIndex;
    uint historyColorTextureIndex;
    uint depthTextureIndex;
    uint normalTextureIndex;
    uint materialTextureIndex;
    uint frameIndex;
    float historyMipCount;
    // Last, so the block ends on the 8 byte alignment the ivec2s give it and its size matches
    // sizeof on the matching struct
    ivec2 outputSize;
    ivec2 hitSize;
} pc;

// Reusing a fixed quad of rays would make the reuse pattern itself a 2x2 feature rather than noise,
// which the temporal pass would then lock onto instead of averaging away
const ivec2 NEIGHBOUR_OFFSETS[4] = ivec2[4](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));

// The mip whose texels cover the specular cone where the ray landed, so one tap stands in for the
// many the lobe would otherwise need. Nothing is cone traced: the footprint is an analytic estimate
// of the lobe's spread at the hit, and reading a prefiltered mip of it is the whole trick.
//
// Contact hardening is not a special case here, it falls straight out. A short hit distance gives a
// narrow footprint and so a low mip, which reads sharp, while a distant hit widens and blurs.
float coneFootprintMip(CameraGPUData _cam, float _roughness, float _hitDistance, float _hitLinearDepth, float _NdotV,
                       vec2 _outputSize) {
    // Half angle of the lobe, as a tangent, from the GGX alpha
    float alpha = _roughness * _roughness;
    float coneTangent = alpha / max(1.0 - alpha, 1e-3);

    // A circular cone is a poor stand-in for the lobe at grazing angles, where it stretches along
    // the view. Narrowing the footprint there stops the reflection smearing across the surface.
    coneTangent *= mix(clamp(_NdotV * 2.0, 0.0, 1.0), 1.0, sqrt(_roughness));
    coneTangent *= biasedConeScale();

    // The cone opens over the distance travelled, giving a radius in world units. Turning that into
    // texels means projecting it at the depth it lands at, not at the depth of the pixel shading it.
    float worldRadius = coneTangent * _hitDistance;
    float texelRadius = worldRadius * abs(_cam.proj[1][1]) * 0.5 * _outputSize.y / max(_hitLinearDepth, 1e-3);

    return clamp(log2(max(texelRadius, 1.0)), 0.0, pc.historyMipCount - 1.0);
}

void main() {
    ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(dst, pc.outputSize))) {
        return;
    }

    float depth = texelFetch(gTextures[nonuniformEXT(pc.depthTextureIndex)], dst, 0).r;
    if (depth >= 1.0) {
        imageStore(outResolved, dst, vec4(0.0, 0.0, 0.0, -1.0));
        return;
    }

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];

    vec2 uv = (vec2(dst) + 0.5) / vec2(pc.outputSize);
    vec4 worldH = cam.invViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec3 worldPos = worldH.xyz / worldH.w;

    vec3 worldNormal = octDecodeNormal(texelFetch(gTextures[nonuniformEXT(pc.normalTextureIndex)], dst, 0).rg);
    float roughness = max(texelFetch(gTextures[nonuniformEXT(pc.materialTextureIndex)], dst, 0).g, MIN_GGX_ROUGHNESS);

    vec3 viewPos = (cam.view * vec4(worldPos, 1.0)).xyz;
    vec3 viewNormal = normalize(mat3(cam.view) * worldNormal);
    vec3 viewDirToEye = normalize(-viewPos);
    float NdotV = max(dot(viewNormal, viewDirToEye), 1e-4);

    // Sliding the window rather than reordering it is what makes the reuse pattern move: a fixed
    // quad would make the 2x2 block itself a feature the temporal pass locks onto instead of
    // averaging away.
    //
    // The slide has to vary across the image as well as over time. Driving it from the frame alone
    // shifts every pixel's gather in the same direction at once, which reads as the whole
    // reflection pulsing in step rather than as noise that averages out.
    uint windowPhase = (pc.frameIndex + uint(dst.x) + uint(dst.y)) & 3u;
    ivec2 windowOrigin = dst / 2 - NEIGHBOUR_OFFSETS[windowPhase];

    // The estimator is a ratio: each neighbour's radiance is weighted by this pixel's own lobe over
    // the density the neighbour actually sampled from, then divided by the summed weight. Dividing
    // by the summed weight is what handles a neighbour describing a different surface, and it also
    // cancels the unknown constant a biased sampling warp would introduce. Because the same weight
    // appears above and below, the result is a convex combination of the taps and so can never
    // exceed the brightest of them, whatever the individual weights come out as.
    vec3 radianceSum = vec3(0.0);
    float weightSum = 0.0;
    float distanceSum = 0.0;

    for (int i = 0; i < 4; ++i) {
        ivec2 coord = clamp(windowOrigin + NEIGHBOUR_OFFSETS[i], ivec2(0), pc.hitSize - 1);
        vec4 hit = texelFetch(gTextures[nonuniformEXT(pc.hitTextureIndex)], coord, 0);

        float pdf = hit.w;
        if (pdf <= 0.0) {
            continue;
        }

        vec3 hitViewPos = cameraViewPositionFromLinearDepth(cam, hit.xy, hit.z);
        vec3 toHit = hitViewPos - viewPos;
        float hitDistance = length(toHit);
        if (hitDistance <= 0.0) {
            continue;
        }

        vec3 L = toHit / hitDistance;
        float NdotL = dot(viewNormal, L);
        if (NdotL <= 0.0) {
            continue;
        }

        vec3 H = normalize(L + viewDirToEye);
        float NdotH = max(dot(viewNormal, H), 0.0);

        float weight = specularLobeWeight(NdotH, NdotV, NdotL, roughness) / pdf;
        float mip = coneFootprintMip(cam, roughness, hitDistance, hit.z, NdotV, vec2(pc.outputSize));

        // The ray landed at a position on this frame's screen, but the only lit colour available is
        // last frame's. Carrying the hit back along the motion stored at it is what keeps the
        // reflection attached to the surface it is reflecting instead of sliding across it.
        ivec2 hitPixel = clamp(ivec2(hit.xy * vec2(pc.outputSize)), ivec2(0), pc.outputSize - 1);
        vec2 hitMotion = texelFetch(gTextures[nonuniformEXT(pc.normalTextureIndex)], hitPixel, 0).zw;
        vec2 historyUV = hit.xy - hitMotion;

        radianceSum += textureLod(gTextures[nonuniformEXT(pc.historyColorTextureIndex)], historyUV, mip).rgb * weight;
        weightSum += weight;
        distanceSum += hitDistance * weight;
    }

    // A negative distance is what marks the record unusable, since a real one never is
    if (weightSum <= 0.0) {
        imageStore(outResolved, dst, vec4(0.0, 0.0, 0.0, -1.0));
        return;
    }

    // Alpha carries how far away the reflection is, averaged over the same rays and the same weights
    // that produced the radiance. The temporal pass reprojects along it, and taking the mean rather
    // than one ray's own draw is what stops that reprojection wandering between frames.
    float hitDistance = distanceSum / weightSum;

    imageStore(outResolved, dst, vec4(radianceSum / weightSum, hitDistance));
}

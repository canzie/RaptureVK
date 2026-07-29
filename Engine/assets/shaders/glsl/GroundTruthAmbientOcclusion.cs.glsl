#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "common/CameraCommon.glsl"
#include "common/Octahedral.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, rgba16f) uniform restrict writeonly image2D outOcclusion;

layout(push_constant) uniform PushConstants {
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint depthTextureIndex;
    uint normalTextureIndex;
    uint linearDepthTextureIndex;
    uint frameIndex;
    float radius;
    float maxScreenRadius;
    float falloffRange;
    int sliceCount;
    int stepCount;
    // Last, so the block ends on the 8 byte alignment the ivec2 gives it and its size matches
    // sizeof on the matching struct
    ivec2 outputSize;
} pc;

const float PI = 3.14159265359;
const float HALF_PI = 1.57079632679;

// The rotation and offset a frame uses, from Jimenez et al. Six rotations and four offsets means a
// pixel repeats every twenty four frames, and every value in between is one a neighbour or an
// earlier frame did not use.
const float TEMPORAL_ROTATIONS[6] = float[](60.0, 300.0, 180.0, 240.0, 120.0, 0.0);
const float TEMPORAL_OFFSETS[4] = float[](0.0, 0.5, 0.25, 0.75);

// Which slice direction a pixel starts from, as a fraction of a half turn. Neighbouring pixels take
// different directions, so the spatial filter that follows recovers slices this pixel never traced.
float spatialDirectionNoise(ivec2 _coord) {
    return (1.0 / 16.0) * float((((_coord.y - _coord.x) & 3) << 2) + (_coord.x & 3));
}

// How far into its first step a pixel starts, as a fraction of one step
float spatialOffsetNoise(ivec2 _coord) {
    return 0.25 * float((_coord.y - _coord.x) & 3);
}

// The cosine between the view vector and the direction to one screen-space sample, which is how far
// that sample would close the horizon in. Faded back to the unoccluded horizon as the sample leaves
// the radius, so geometry crossing the boundary does not pop.
float horizonCosAt(CameraGPUData _cam, vec3 _viewPos, vec3 _view, ivec2 _coord, float _lowHorizonCos,
                   float _falloffStart, float _falloffEnd) {
    // Off screen is not evidence of an occluder, it is an absence of evidence
    if (any(lessThan(_coord, ivec2(0))) || any(greaterThanEqual(_coord, pc.outputSize))) {
        return _lowHorizonCos;
    }

    float sampleDepth = texelFetch(gTextures[nonuniformEXT(pc.linearDepthTextureIndex)], _coord, 0).r;
    vec2 sampleUV = (vec2(_coord) + 0.5) / vec2(pc.outputSize);
    vec3 delta = cameraViewPositionFromLinearDepth(_cam, sampleUV, sampleDepth) - _viewPos;

    float sampleDistance = length(delta);
    if (sampleDistance < 1e-5) {
        return _lowHorizonCos;
    }

    float falloff = clamp((sampleDistance - _falloffStart) / max(_falloffEnd - _falloffStart, 1e-5), 0.0, 1.0);
    return mix(dot(delta, _view) / sampleDistance, _lowHorizonCos, falloff);
}

// Cosine weighted visibility of the arc a slice leaves open, integrated in closed form rather than
// by counting samples. This is what separates ground truth ambient occlusion from the horizon based
// methods it descends from: the same horizons feed an exact integral instead of an average.
float sliceVisibility(float _h1, float _h2, float _n) {
    return 0.25 * (-cos(2.0 * _h1 - _n) + cos(_n) + 2.0 * _h1 * sin(_n)) +
           0.25 * (-cos(2.0 * _h2 - _n) + cos(_n) + 2.0 * _h2 * sin(_n));
}

// The centroid of that same arc, as a direction in the slice plane. Averaged across slices it gives
// the direction the surface is actually open towards, which carries the shape of the occlusion that
// a single visibility number throws away.
vec3 sliceBentDirection(float _h1, float _h2, float _n, vec3 _view, vec3 _tangent) {
    float alongTangent = (6.0 * sin(_h1 - _n) - sin(3.0 * _h1 - _n) + 6.0 * sin(_h2 - _n) - sin(3.0 * _h2 - _n) +
                          16.0 * sin(_n) - 3.0 * (sin(_h1 + _n) + sin(_h2 + _n))) /
                         12.0;
    float alongView = (-cos(3.0 * _h1 - _n) - cos(3.0 * _h2 - _n) + 8.0 * cos(_n) -
                       3.0 * (cos(_h1 + _n) + cos(_h2 + _n))) /
                      12.0;
    return alongTangent * _tangent + alongView * _view;
}

void main() {
    ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(dst, pc.outputSize))) {
        return;
    }

    float depth = texelFetch(gTextures[nonuniformEXT(pc.depthTextureIndex)], dst, 0).r;
    if (depth >= 1.0) {
        imageStore(outOcclusion, dst, vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];

    vec2 uv = (vec2(dst) + 0.5) / vec2(pc.outputSize);
    float linearDepth = texelFetch(gTextures[nonuniformEXT(pc.linearDepthTextureIndex)], dst, 0).r;
    vec3 viewPos = cameraViewPositionFromLinearDepth(cam, uv, linearDepth);
    vec3 view = normalize(-viewPos);

    vec3 worldNormal = octDecodeNormal(texelFetch(gTextures[nonuniformEXT(pc.normalTextureIndex)], dst, 0).rg);
    vec3 viewNormal = normalize(mat3(cam.view) * worldNormal);

    // A world radius covers fewer pixels the further away it is. The lower clamp keeps every step
    // landing on a texel it has not already read, the upper one bounds what a surface right against
    // the camera can cost.
    float pixelsPerUnit = cam.proj[0][0] * 0.5 * float(pc.outputSize.x) / linearDepth;
    float screenRadius = clamp(pc.radius * pixelsPerUnit, float(pc.stepCount), pc.maxScreenRadius);

    // Clamping the screen radius changes the world distance it stands for, and the falloff has to
    // follow it or an occluder would fade out somewhere other than where sampling stops
    float worldRadius = screenRadius / pixelsPerUnit;
    float falloffStart = worldRadius * (1.0 - pc.falloffRange);
    float stepSize = screenRadius / float(pc.stepCount);

    float rotation = spatialDirectionNoise(dst) + TEMPORAL_ROTATIONS[pc.frameIndex % 6u] / 360.0;
    float offset = fract(spatialOffsetNoise(dst) + TEMPORAL_OFFSETS[(pc.frameIndex / 6u) % 4u]);

    float visibility = 0.0;
    vec3 bentNormal = vec3(0.0);

    for (int slice = 0; slice < pc.sliceCount; ++slice) {
        // A slice is a line through the pixel, not a ray, so the directions only span half a turn
        float phi = (float(slice) / float(pc.sliceCount) + rotation) * PI;
        vec2 direction = vec2(cos(phi), sin(phi));

        // The view space direction this screen space march corresponds to. Undoing the projection
        // rather than reusing the screen direction is what keeps the frame the right way round:
        // under Vulkan's downward y and a non-square aspect the two do not agree, and a mirrored
        // frame swaps the two horizons and reflects every bent normal.
        vec3 marchDirection = vec3(direction.x / cam.proj[0][0], direction.y / cam.proj[1][1], 0.0);

        // The slice is the plane holding both the view vector and the direction marched along. Its
        // normal is the axis both horizon angles are measured about.
        vec3 slicePlaneNormal = cross(marchDirection, view);
        float slicePlaneNormalLength = length(slicePlaneNormal);
        if (slicePlaneNormalLength < 1e-5) {
            continue;
        }
        slicePlaneNormal /= slicePlaneNormalLength;

        // In the slice plane and perpendicular to the view vector, pointing the way the positive
        // march walks
        vec3 tangent = normalize(cross(view, slicePlaneNormal));

        vec3 projectedNormal = viewNormal - slicePlaneNormal * dot(viewNormal, slicePlaneNormal);
        float projectedNormalLength = length(projectedNormal);
        if (projectedNormalLength < 1e-5) {
            continue;
        }

        // Signed angle of the surface normal from the view vector, which every horizon in this slice
        // is measured against
        float n = atan(dot(projectedNormal, tangent), dot(projectedNormal, view));

        // The arc a slice can integrate is where the normal's hemisphere and the camera's overlap.
        // Screen space holds no record of anything behind the view vector, so the horizon starts at
        // whichever of the two limits is tighter and samples only ever close it further.
        float lowHorizonCos1 = cos(max(-HALF_PI, n - HALF_PI));
        float lowHorizonCos2 = cos(min(HALF_PI, n + HALF_PI));

        float horizonCos1 = lowHorizonCos1;
        float horizonCos2 = lowHorizonCos2;

        for (int stepIndex = 0; stepIndex < pc.stepCount; ++stepIndex) {
            // The extra pixel keeps the first sample off the pixel being shaded
            ivec2 delta = ivec2(round(direction * ((float(stepIndex) + offset) * stepSize + 1.0)));

            horizonCos1 = max(horizonCos1, horizonCosAt(cam, viewPos, view, dst - delta, lowHorizonCos1,
                                                        falloffStart, worldRadius));
            horizonCos2 = max(horizonCos2, horizonCosAt(cam, viewPos, view, dst + delta, lowHorizonCos2,
                                                        falloffStart, worldRadius));
        }

        float h1 = -acos(clamp(horizonCos1, -1.0, 1.0));
        float h2 = acos(clamp(horizonCos2, -1.0, 1.0));

        // Weighted by how much of the normal this slice actually holds, which is the Jacobian of
        // projecting the hemisphere onto the slice plane
        visibility += projectedNormalLength * sliceVisibility(h1, h2, n);
        bentNormal += projectedNormalLength * sliceBentDirection(h1, h2, n, view, tangent);
    }

    visibility /= float(pc.sliceCount);

    // Slices that cancel leave no direction to report, and the surface normal is the answer a
    // consumer would have used without a bent normal at all
    float bentNormalLength = length(bentNormal);
    vec3 worldBentNormal = bentNormalLength > 1e-5 ? transpose(mat3(cam.view)) * (bentNormal / bentNormalLength) : worldNormal;

    imageStore(outOcclusion, dst, vec4(worldBentNormal, clamp(visibility, 0.0, 1.0)));
}

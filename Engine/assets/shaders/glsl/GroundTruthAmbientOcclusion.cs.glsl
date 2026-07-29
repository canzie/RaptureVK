#version 460

#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, r16f) uniform restrict writeonly image2D outAmbientOcclusion;

#include "common/CameraCommon.glsl"
#include "common/Octahedral.glsl"

layout(push_constant) uniform PushConstants {
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint depthTextureIndex;
    uint normalTextureIndex;
    ivec2 outputSize;
} pc;

const uint SLICE_COUNT = 3;
const uint STEP_COUNT = 4;
const float WORLD_RADIUS = 1.0;
const float MAX_SCREEN_RADIUS = 128.0;
const float PI = 3.14159265;
const float HALF_PI = 1.57079632679;
const float FALLOFF_RANGE = 0.25;

float spatialDirectionNoise(ivec2 c) {
    return (1.0 / 16.0) * float((((c.y - c.x) & 3) << 2) + (c.x & 3));
}

float spatialOffsetNoise(ivec2 c) {
    return 0.25 * float((c.y - c.x) & 3);
}

float marchHorizon(vec3 P, vec3 viewDir, vec2 dir, float stepSize, ivec2 coords, CameraGPUData cam, float cOffset,
                   float worldRadius, float falloffStart, float lowHorizonCos) {
    float horizonCos = lowHorizonCos;

    for (uint i = 1; i <= STEP_COUNT; ++i) {
        ivec2 c = coords + ivec2(round(dir * ((float(i) + cOffset) * stepSize)));
        if (any(lessThan(c, ivec2(0))) || any(greaterThanEqual(c, pc.outputSize))) {
            break;                                  // off screen: no evidence, not an occluder
        }

        float d = cameraLinearDepth(cam, texelFetch(gTextures[nonuniformEXT(pc.depthTextureIndex)], c, 0).r);
        vec2  suv = (vec2(c) + 0.5) / vec2(pc.outputSize);
        vec3  D = cameraViewPositionFromLinearDepth(cam, suv, d) - P;
        float dist = length(D);

        float falloff = clamp((dist - falloffStart) / max(worldRadius - falloffStart, 1e-5), 0.0, 1.0);

        horizonCos = max(horizonCos, mix(dot(D / dist, viewDir), lowHorizonCos, falloff));
    }

    return horizonCos;
}

// Cosine weighted visibility of the arc a slice leaves open, both horizons measured from the view
// vector and n the angle of the projected normal
float sliceVisibility(float h1, float h2, float n) {
    return 0.25 * (-cos(2.0 * h1 - n) + cos(n) + 2.0 * h1 * sin(n)) +
           0.25 * (-cos(2.0 * h2 - n) + cos(n) + 2.0 * h2 * sin(n));
}

void main() {
    ivec2 coords = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(coords, pc.outputSize))) {
        return;
    }

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];
    float depth = texelFetch(gTextures[nonuniformEXT(pc.depthTextureIndex)], coords, 0).r;

    if (depth >= 1.0) {
        imageStore(outAmbientOcclusion, coords, vec4(1.0));
        return;
    }

    float linDepth = cameraLinearDepth(cam, depth);
    vec2 normal = texelFetch(gTextures[nonuniformEXT(pc.normalTextureIndex)], coords, 0).rg;
    vec3 worldNormal = octDecodeNormal(normal);
    vec3 viewNormal = normalize(mat3(cam.view) * worldNormal);

    float pixelsPerUnit = cam.proj[0][0] * 0.5 * float(pc.outputSize.x) / linDepth;
    float screenRadius = clamp(WORLD_RADIUS * pixelsPerUnit, float(STEP_COUNT), MAX_SCREEN_RADIUS);
    float stepSize = screenRadius / float(STEP_COUNT);

    // The clamp changes the world distance the march actually covers, so the falloff is derived from
    // what was covered rather than from what was asked for
    float worldRadius = screenRadius / pixelsPerUnit;
    float falloffStart = worldRadius * (1.0 - FALLOFF_RANGE);

    vec2 uv = (vec2(coords) + 0.5) / vec2(pc.outputSize);
    vec3 P = cameraViewPositionFromLinearDepth(cam, uv, linDepth);
    
    float sliceRotation = spatialDirectionNoise(coords);
    float coordOffset = spatialOffsetNoise(coords);

    vec3 viewDir = normalize(-P);

    float sum = 0.0;
    float openSum = 0.0;

    for (uint slice=0; slice < SLICE_COUNT; ++slice) {
        float phi = (float(slice)/float(SLICE_COUNT) + sliceRotation) * PI;
        vec2 dir = vec2(cos(phi), sin(phi));

        // Undoing the projection is what keeps the frame the right way round. Reusing the screen
        // direction mirrors it under Vulkan's downward y and a non square aspect, which swaps the
        // two horizons.
        vec3 marchDir = vec3(dir.x / cam.proj[0][0], dir.y / cam.proj[1][1], 0.0);

        vec3 slicePlaneNormal = cross(marchDir, viewDir);
        float slicePlaneNormalLength = length(slicePlaneNormal);
        if (slicePlaneNormalLength < 1e-5) {
            continue;
        }
        slicePlaneNormal /= slicePlaneNormalLength;

        vec3 tangent = normalize(cross(viewDir, slicePlaneNormal));

        vec3 projN = viewNormal - slicePlaneNormal * dot(viewNormal, slicePlaneNormal);
        float projNLength = length(projN);
        if (projNLength < 1e-5) {
            continue;
        }

        float n = atan(dot(projN, tangent), dot(projN, viewDir));

        // The arc is where the normal's hemisphere and the camera's overlap, so the horizon starts
        // at whichever limit is tighter and samples only ever close it further
        float lowHorizonCos1 = cos(max(-HALF_PI, n - HALF_PI));
        float lowHorizonCos2 = cos(min(HALF_PI, n + HALF_PI));

        float horizonCos1 = marchHorizon(P, viewDir, -dir, stepSize, coords, cam, coordOffset, worldRadius,
                                         falloffStart, lowHorizonCos1);
        float horizonCos2 = marchHorizon(P, viewDir, dir, stepSize, coords, cam, coordOffset, worldRadius,
                                         falloffStart, lowHorizonCos2);

        float h1 = -acos(clamp(horizonCos1, -1.0, 1.0));
        float h2 = acos(clamp(horizonCos2, -1.0, 1.0));

        sum += projNLength * sliceVisibility(h1, h2, n);

        // The same integral over the arc before any sample closed it. Dividing by this rather than
        // by the slice count keeps the half of the hemisphere behind the view plane out of the
        // answer, which would otherwise darken every silhouette by around half.
        float openH1 = -acos(clamp(lowHorizonCos1, -1.0, 1.0));
        float openH2 = acos(clamp(lowHorizonCos2, -1.0, 1.0));
        openSum += projNLength * sliceVisibility(openH1, openH2, n);
    }

    float ao = openSum > 1e-5 ? sum / openSum : 1.0;

    imageStore(outAmbientOcclusion, coords, vec4(ao));


}

#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "common/CameraCommon.glsl"
#include "common/ImportanceSampling.glsl"
#include "common/Octahedral.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, rgba32f) uniform restrict writeonly image2DArray outHit;

// One entry per allocated ray, written by the allocation pass. A workgroup is one tile's one ray
// slot, so unallocated slots are never dispatched rather than dispatched and retired.
layout(std430, set = 4, binding = 1) readonly buffer WorkItems {
    uvec2 items[];
} u_workItems;

layout(push_constant) uniform PushConstants {
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint depthTextureIndex;
    uint normalTextureIndex;
    uint materialTextureIndex;
    uint linearDepthTextureIndex;
    uint tileCountX;
    uint frameIndex;
    float maxDistance;
    float thickness;
    int stepCount;
    int hiZMaxLevel;
    // Last, so the block ends on the 8 byte alignment the ivec2s give it and its size matches
    // sizeof on the matching struct
    ivec2 outputSize;
    ivec2 fullResSize;
} pc;

// The march's step growth per iteration. Anything above 1 samples the near range finely without
// making a long ray unaffordable.
const float STEP_GROWTH = 1.05;

// Length of the sample sequence the frame index walks. Long enough that the pattern does not repeat
// inside a temporal accumulation window.
const uint SAMPLE_SEQUENCE_LENGTH = 64u;

// Attempts before a pixel gives up on producing a ray above its own surface. A grazing view of a
// rough surface has a real chance of sampling a microfacet that reflects downwards.
const int MAX_RAY_ATTEMPTS = 4;

// Bisections narrowing an accepted step down onto the crossing. Only hits pay for these.
const int REFINEMENT_STEPS = 5;

// How far the ray starts off its own surface, as a fraction of the distance to the camera
const float RAY_ORIGIN_BIAS = 0.005;

// The pyramid walk is only worth its cost where the lobe is tight enough that a missed thin surface
// would be visible. Above this the cheap march is blurred past recognition anyway.
const float HIZ_ROUGHNESS_LIMIT = 0.2;

// Enough for a walk that ascends on every clear cell, and a hard stop if one fails to converge
const int MAX_HIZ_ITERATIONS = 96;

// Pushes the ray just over a cell boundary so the next read lands in the neighbour, as a fraction
// of the cell it is leaving. A fixed uv distance would be a rounding error at the coarse levels and
// most of a texel at the finest, where overshooting skips cells the walk exists to visit.
const float HIZ_CROSS_EPSILON = 1.0 / 128.0;

// Keeps the far end of the ray in front of the eye plane, where the projection stays finite
const float HIZ_NEAR_CLAMP = 0.01;

struct TraceResult {
    vec2 hitUV;
    float hitLinearDepth;
    bool hit;
};

uint hashUint(uint _x) {
    _x ^= _x >> 16;
    _x *= 0x7feb352du;
    _x ^= _x >> 15;
    _x *= 0x846ca68bu;
    _x ^= _x >> 16;
    return _x;
}

float hashToUnitFloat(uint _x) {
    return float(hashUint(_x) & 0x00FFFFFFu) / float(0x01000000u);
}

// Marches the reflected ray in view space, comparing against the linear depth pyramid's mip 0.
TraceResult ssrMarch(CameraGPUData _cam, vec3 _viewPos, vec3 _viewDir) {
    TraceResult result;
    result.hitUV = vec2(0.0);
    result.hitLinearDepth = 0.0;
    result.hit = false;

    // Normalised so the geometric series over stepCount steps sums to exactly maxDistance
    float stepSize = pc.maxDistance * (STEP_GROWTH - 1.0) / (pow(STEP_GROWTH, float(pc.stepCount)) - 1.0);
    float t = 0.0;
    float previousDiff = -1.0;

    for (int i = 0; i < pc.stepCount; ++i) {
        t += stepSize;

        vec3 p = _viewPos + _viewDir * t;

        // View z is negative in front of the camera, so a non-negative z has crossed the eye plane
        if (p.z >= 0.0) {
            break;
        }

        vec4 clip = _cam.proj * vec4(p, 1.0);
        vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
            break;
        }

        float rayDepth = -p.z;
        float sceneDepth = textureLod(gTextures[nonuniformEXT(pc.linearDepthTextureIndex)], uv, 0.0).r;
        float diff = rayDepth - sceneDepth;

        // A hit needs the ray to cross the surface during this step, so the previous sample has to
        // have been in front of it. That rejects a ray already travelling behind geometry. The
        // depth it may end up behind is bounded by the step, since a step that overshoots a solid
        // surface lands that far past it.
        if (diff > 0.0 && previousDiff <= 0.0 && diff < max(pc.thickness, stepSize)) {
            // The crossing happened somewhere inside the step just taken, so without refinement the
            // hit snaps to wherever the step landed. That quantisation is what draws the bands: the
            // whole region accepted at one step index shares a hit location.
            float low = t - stepSize;
            float high = t;
            for (int refine = 0; refine < REFINEMENT_STEPS; ++refine) {
                float mid = 0.5 * (low + high);
                vec3 midPoint = _viewPos + _viewDir * mid;
                vec4 midClip = _cam.proj * vec4(midPoint, 1.0);
                vec2 midUV = (midClip.xy / midClip.w) * 0.5 + 0.5;
                float midScene = textureLod(gTextures[nonuniformEXT(pc.linearDepthTextureIndex)], midUV, 0.0).r;

                if (-midPoint.z > midScene) {
                    high = mid;
                } else {
                    low = mid;
                }
            }

            vec3 hitPoint = _viewPos + _viewDir * high;
            vec4 hitClip = _cam.proj * vec4(hitPoint, 1.0);
            result.hitUV = (hitClip.xy / hitClip.w) * 0.5 + 0.5;
            result.hitLinearDepth = textureLod(gTextures[nonuniformEXT(pc.linearDepthTextureIndex)], result.hitUV, 0.0).r;
            result.hit = true;
            return result;
        }

        previousDiff = diff;
        stepSize *= STEP_GROWTH;
    }

    return result;
}

vec2 hiZCell(vec2 _uv, vec2 _cellCount) {
    return floor(_uv * _cellCount);
}

// Where the ray leaves the cell it currently occupies, nudged just past the boundary so the next
// iteration reads the neighbouring cell rather than landing back on this one
vec3 hiZIntersectCellBoundary(vec3 _o, vec3 _d, vec2 _cellIdx, vec2 _cellCount, vec2 _crossStep, vec2 _crossOffset) {
    vec2 boundary = (_cellIdx + _crossStep) / _cellCount + _crossOffset;
    vec2 delta = (boundary - _o.xy) / _d.xy;
    return _o + _d * min(delta.x, delta.y);
}

// A stackless walk of the min-Z pyramid. A level holds the nearest surface anywhere beneath it, so
// a ray passing in front of a cell at some level is guaranteed to clear everything that cell covers
// and can skip the lot in one step. Descending only where it cannot clear a cell means the walk
// never steps over a surface, which is what the linear march cannot promise however fine its steps.
TraceResult hiZMarch(CameraGPUData _cam, vec3 _viewPos, vec3 _viewDir) {
    TraceResult result;
    result.hitUV = vec2(0.0);
    result.hitLinearDepth = 0.0;
    result.hit = false;

    // The ray has to be built in screen space, where a projective transform keeps it straight
    vec3 endView = _viewPos + _viewDir * pc.maxDistance;
    if (endView.z > -HIZ_NEAR_CLAMP) {
        endView = _viewPos + _viewDir * ((-HIZ_NEAR_CLAMP - _viewPos.z) / _viewDir.z);
    }

    vec4 clipStart = _cam.proj * vec4(_viewPos, 1.0);
    vec4 clipEnd = _cam.proj * vec4(endView, 1.0);
    vec3 ndcStart = clipStart.xyz / clipStart.w;
    vec3 ndcEnd = clipEnd.xyz / clipEnd.w;

    vec3 o = vec3(ndcStart.xy * 0.5 + 0.5, ndcStart.z);
    vec3 d = vec3(ndcEnd.xy * 0.5 + 0.5, ndcEnd.z) - o;
    if (abs(d.x) < 1e-7) {
        d.x = 1e-7;
    }
    if (abs(d.y) < 1e-7) {
        d.y = 1e-7;
    }

    ivec2 baseSize = textureSize(gTextures[nonuniformEXT(pc.linearDepthTextureIndex)], 0);

    vec2 crossStep = vec2(d.x >= 0.0 ? 1.0 : 0.0, d.y >= 0.0 ? 1.0 : 0.0);

    vec3 ray = o;
    int level = 0;
    int iterations = 0;

    while (level >= 0 && iterations < MAX_HIZ_ITERATIONS) {
        ++iterations;

        vec2 cellCount = vec2(max(baseSize >> level, ivec2(1)));
        vec2 crossOffset = (crossStep * 2.0 - 1.0) * (HIZ_CROSS_EPSILON / cellCount);
        vec2 oldCell = hiZCell(ray.xy, cellCount);

        float cellLinear = texelFetch(gTextures[nonuniformEXT(pc.linearDepthTextureIndex)], ivec2(oldCell), level).r;
        float cellNdc = cameraNdcDepthFromLinear(_cam, cellLinear);

        vec3 stepped = ray;
        if (d.z > 0.0 && cellNdc > ray.z) {
            stepped = o + d * ((cellNdc - o.z) / d.z);
        }

        // Reaching the cell's depth without leaving the cell means the surface is inside it, so the
        // walk descends. Leaving first means the cell is clear and the ray moves on one level up.
        if (hiZCell(stepped.xy, cellCount) != oldCell) {
            stepped = hiZIntersectCellBoundary(o, d, oldCell, cellCount, crossStep, crossOffset);
            level = min(pc.hiZMaxLevel, level + 1);
        } else {
            --level;
        }

        ray = stepped;

        if (any(lessThan(ray.xy, vec2(0.0))) || any(greaterThan(ray.xy, vec2(1.0))) || ray.z > 1.0) {
            return result;
        }
    }

    if (level >= 0) {
        return result;
    }

    float sceneLinear = textureLod(gTextures[nonuniformEXT(pc.linearDepthTextureIndex)], ray.xy, 0.0).r;
    float rayLinear = cameraLinearDepth(_cam, ray.z);

    // Descending to level -1 only proves the ray reached the nearest surface in that cell, not that
    // it stopped at a surface facing it, so the same thickness test the linear march uses applies
    if (abs(rayLinear - sceneLinear) > pc.thickness) {
        return result;
    }

    result.hitUV = ray.xy;
    result.hitLinearDepth = sceneLinear;
    result.hit = true;
    return result;
}

void main() {
    // The workgroup index selects a tile and a ray slot from the compacted list rather than being a
    // position on screen, so the pixel this invocation owns has to be rebuilt from the tile
    uvec2 workItem = u_workItems.items[gl_WorkGroupID.x];
    uvec2 tile = uvec2(workItem.x % pc.tileCountX, workItem.x / pc.tileCountX);
    int rayIndex = int(workItem.y);

    ivec2 dst = ivec2(tile * gl_WorkGroupSize.xy + gl_LocalInvocationID.xy);
    if (any(greaterThanEqual(dst, pc.outputSize))) {
        return;
    }

    ivec2 srcCoord = min(dst * 2, pc.fullResSize - 1);

    float depth = texelFetch(gTextures[nonuniformEXT(pc.depthTextureIndex)], srcCoord, 0).r;
    if (depth >= 1.0) {
        imageStore(outHit, ivec3(dst, rayIndex), vec4(0.0));
        return;
    }

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];

    vec2 uv = (vec2(srcCoord) + 0.5) / vec2(pc.fullResSize);
    vec4 worldH = cam.invViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec3 worldPos = worldH.xyz / worldH.w;

    vec3 worldNormal = octDecodeNormal(texelFetch(gTextures[nonuniformEXT(pc.normalTextureIndex)], srcCoord, 0).rg);
    float roughness = max(texelFetch(gTextures[nonuniformEXT(pc.materialTextureIndex)], srcCoord, 0).g, MIN_GGX_ROUGHNESS);

    vec3 viewPos = (cam.view * vec4(worldPos, 1.0)).xyz;
    vec3 viewNormal = normalize(mat3(cam.view) * worldNormal);
    vec3 viewDirToEye = normalize(-viewPos);

    // The lobe is sampled in the surface's own frame, where the normal is z
    mat3 tangentFrame = buildTangentFrame(viewNormal);
    vec3 tangentV = normalize(transpose(tangentFrame) * viewDirToEye);
    if (tangentV.z <= 0.0) {
        imageStore(outHit, ivec3(dst, rayIndex), vec4(0.0));
        return;
    }

    // Decorrelating the sequence per pixel turns what would be a repeating screen-wide pattern into
    // noise the resolve and the temporal pass can average away
    uint pixelSeed = hashUint(uint(dst.x) * 73856093u ^ uint(dst.y) * 19349663u);
    vec2 jitter = vec2(hashToUnitFloat(pixelSeed), hashToUnitFloat(pixelSeed ^ 0x9e3779b9u));

    vec3 tangentL = vec3(0.0);
    float pdf = 0.0;

    for (int attempt = 0; attempt < MAX_RAY_ATTEMPTS; ++attempt) {
        // The step between attempts has to be smaller than the sequence, or every attempt lands on
        // the same point and redraws the ray it just rejected
        uint sampleIndex =
            (pc.frameIndex + uint(attempt) + uint(rayIndex) * uint(MAX_RAY_ATTEMPTS) + pixelSeed) % SAMPLE_SEQUENCE_LENGTH;
        vec2 u = fract(hammersley(sampleIndex, SAMPLE_SEQUENCE_LENGTH) + jitter);

        // Zero is the mirror end of this parameterisation: it puts the sample at the centre of the
        // projected disk, which unstretches to the peak of the visible lobe
        u.x = mix(u.x, 0.0, GGX_SAMPLING_BIAS);

        vec3 tangentH = importanceSampleGgxVndf(u, tangentV, roughness);
        tangentL = reflect(-tangentV, tangentH);

        // A microfacet can face the viewer while still reflecting into the surface. Those carry no
        // energy, so the ray is redrawn rather than bent back out, which would bias the lobe.
        if (tangentL.z > 0.0) {
            pdf = pdfGgxVndf(max(tangentH.z, 0.0), tangentV.z, roughness);
            break;
        }
    }

    if (pdf <= 0.0) {
        imageStore(outHit, ivec3(dst, rayIndex), vec4(0.0));
        return;
    }

    vec3 viewDir = normalize(tangentFrame * tangentL);

    // Lifting the origin off the surface stops the first step from registering against the surface
    // it started on. A ray leaving at a grazing angle stays within its own depth for a long way, and
    // a false hit there reflects the surface into itself, smeared along the direction it was
    // travelling. The offset scales with depth so it stays the same size on screen.
    vec3 biasedOrigin = viewPos + viewNormal * (RAY_ORIGIN_BIAS * (-viewPos.z));

    // A min-Z pyramid bounds what lies beyond a cell, so the walk can only skip space for a ray
    // getting further from the eye. One heading back toward it never crosses a cell's depth plane,
    // so the walk descends without advancing and lands back on its own origin. View z is negative in
    // front of the camera, so travelling away means a negative z component.
    bool canWalkPyramid = viewDir.z < 0.0;

    TraceResult result = (roughness <= HIZ_ROUGHNESS_LIMIT && canWalkPyramid)
                             ? hiZMarch(cam, biasedOrigin, viewDir)
                             : ssrMarch(cam, biasedOrigin, viewDir);
    if (!result.hit) {
        imageStore(outHit, ivec3(dst, rayIndex), vec4(0.0));
        return;
    }

    // A stored pdf above zero is what marks the record as usable, so no separate mask is needed
    imageStore(outHit, ivec3(dst, rayIndex), vec4(result.hitUV, result.hitLinearDepth, pdf));
}

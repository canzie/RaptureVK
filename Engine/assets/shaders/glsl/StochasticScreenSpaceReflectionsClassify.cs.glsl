#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "common/CameraCommon.glsl"
#include "common/ImportanceSampling.glsl"
#include "common/Octahedral.glsl"

// One workgroup per tile, one invocation per half-resolution pixel in it
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, r32f) uniform restrict writeonly image2D outTileRayCount;

layout(push_constant) uniform PushConstants {
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint depthTextureIndex;
    uint normalTextureIndex;
    uint materialTextureIndex;
    uint linearDepthTextureIndex;
    uint historyColorTextureIndex;
    float maxDistance;
    int minRays;
    int maxRays;
    ivec2 traceSize;
    ivec2 fullResSize;
} pc;

// Tracer rays are deliberately coarse. They exist to estimate how much the reflection varies across
// a tile, not to produce anything that is kept, so a short fixed march is enough.
const int TRACER_STEPS = 16;

// Spacing between tracer rays inside the tile. Four rays over an 8x8 tile of half-resolution pixels
// is one per 8x8 block of full-resolution pixels, matching the talk's eighth-resolution tracer.
const uint TRACER_STRIDE = 4u;

// One slot per tracer rather than a running sum, since a shared float has no atomic add
const uint TRACER_COUNT = (8u / TRACER_STRIDE) * (8u / TRACER_STRIDE);

shared float s_luminance[TRACER_COUNT];
shared uint s_traced[TRACER_COUNT];
shared uint s_hit[TRACER_COUNT];
shared uint s_reflectiveCount;

// Relative luminance. Variance is measured on it rather than on colour because what reads as noise
// is brightness scatter, and a log curve compresses the highlights that would otherwise dominate a
// single bright sample into the whole tile's budget.
float perceptualLuminance(vec3 _color) {
    return log2(1.0 + dot(_color, vec3(0.2126, 0.7152, 0.0722)));
}

void main() {
    uint local = gl_LocalInvocationIndex;
    if (local == 0u) {
        s_reflectiveCount = 0u;
    }
    if (local < TRACER_COUNT) {
        s_luminance[local] = 0.0;
        s_traced[local] = 0u;
        s_hit[local] = 0u;
    }
    barrier();

    ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
    bool inBounds = all(lessThan(dst, pc.traceSize));

    ivec2 srcCoord = min(dst * 2, pc.fullResSize - 1);
    float depth = inBounds ? texelFetch(gTextures[nonuniformEXT(pc.depthTextureIndex)], srcCoord, 0).r : 1.0;
    bool isSurface = depth < 1.0;

    if (isSurface) {
        atomicAdd(s_reflectiveCount, 1u);
    }

    // Only the strided subset traces, so the cost stays at the talk's tracer resolution rather than
    // one ray per pixel
    bool isTracer = (gl_LocalInvocationID.x % TRACER_STRIDE) == 0u && (gl_LocalInvocationID.y % TRACER_STRIDE) == 0u;

    uint tracerIndex = (gl_LocalInvocationID.y / TRACER_STRIDE) * (8u / TRACER_STRIDE) +
                       (gl_LocalInvocationID.x / TRACER_STRIDE);

    if (isSurface && isTracer) {
        s_traced[tracerIndex] = 1u;

        CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];

        vec2 uv = (vec2(srcCoord) + 0.5) / vec2(pc.fullResSize);
        vec4 worldH = cam.invViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
        vec3 worldPos = worldH.xyz / worldH.w;

        vec3 worldNormal = octDecodeNormal(texelFetch(gTextures[nonuniformEXT(pc.normalTextureIndex)], srcCoord, 0).rg);

        vec3 viewPos = (cam.view * vec4(worldPos, 1.0)).xyz;
        vec3 viewNormal = normalize(mat3(cam.view) * worldNormal);

        // The mirror direction stands in for the lobe here. The tile only needs to know roughly what
        // is over there, and a sampled direction would make the estimate itself noisy.
        vec3 viewDir = reflect(normalize(viewPos), viewNormal);

        float stepSize = pc.maxDistance / float(TRACER_STEPS);
        float previousDiff = -1.0;

        for (int i = 1; i <= TRACER_STEPS; ++i) {
            vec3 p = viewPos + viewDir * (stepSize * float(i));
            if (p.z >= 0.0) {
                break;
            }

            vec4 clip = cam.proj * vec4(p, 1.0);
            vec2 hitUV = (clip.xy / clip.w) * 0.5 + 0.5;
            if (any(lessThan(hitUV, vec2(0.0))) || any(greaterThan(hitUV, vec2(1.0)))) {
                break;
            }

            float sceneDepth = textureLod(gTextures[nonuniformEXT(pc.linearDepthTextureIndex)], hitUV, 0.0).r;
            float diff = -p.z - sceneDepth;

            if (diff > 0.0 && previousDiff <= 0.0 && diff < stepSize) {
                vec3 hitColor = textureLod(gTextures[nonuniformEXT(pc.historyColorTextureIndex)], hitUV, 0.0).rgb;
                s_luminance[tracerIndex] = perceptualLuminance(hitColor);
                s_hit[tracerIndex] = 1u;
                break;
            }

            previousDiff = diff;
        }
    }
    barrier();

    if (local != 0u) {
        return;
    }

    int rayCount = 0;
    if (s_reflectiveCount > 0u) {
        uint tracedCount = 0u;
        uint hitCount = 0u;
        float sum = 0.0;
        float squaredSum = 0.0;
        for (uint i = 0u; i < TRACER_COUNT; ++i) {
            tracedCount += s_traced[i];
            if (s_hit[i] == 0u) {
                continue;
            }
            ++hitCount;
            sum += s_luminance[i];
            squaredSum += s_luminance[i] * s_luminance[i];
        }

        // Two things make a tile expensive: the reflection changing a lot across it, and rays
        // disagreeing about whether there is anything to reflect at all. A tile whose tracers all
        // missed still gets the floor, since the real rays are importance sampled and may find
        // something these coarse mirror rays did not.
        float importance = 0.0;
        if (hitCount > 0u) {
            float inverseCount = 1.0 / float(hitCount);
            float mean = sum * inverseCount;
            float deviation = sqrt(max(squaredSum * inverseCount - mean * mean, 0.0));
            float missFraction = 1.0 - float(hitCount) / float(max(tracedCount, 1u));
            importance = clamp(deviation + missFraction, 0.0, 1.0);
        }

        rayCount = clamp(int(ceil(mix(float(pc.minRays), float(pc.maxRays), importance))), pc.minRays, pc.maxRays);
    }

    imageStore(outTileRayCount, ivec2(gl_WorkGroupID.xy), vec4(float(rayCount), 0.0, 0.0, 0.0));
}

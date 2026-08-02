#version 460 core


#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require


#ifdef DDGI_BLEND_RADIANCE
    layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
    #define RTXGI_DDGI_PROBE_NUM_TEXELS 8
    #define RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS (RTXGI_DDGI_PROBE_NUM_TEXELS - 2)
#else
    layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
    #define RTXGI_DDGI_PROBE_NUM_TEXELS 16
    #define RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS (RTXGI_DDGI_PROBE_NUM_TEXELS - 2)
#endif

// Number of rays cached in shared memory per iteration, must equal the workgroup thread count
#define DDGI_BLEND_RAYS_PER_CHUNK (RTXGI_DDGI_PROBE_NUM_TEXELS * RTXGI_DDGI_PROBE_NUM_TEXELS)

// Every texel of a probe blends the same set of rays, so the ray data and the ray directions
// are loaded and computed once per workgroup instead of once per texel
#ifdef DDGI_BLEND_RADIANCE
    shared vec4 sRayData[DDGI_BLEND_RAYS_PER_CHUNK];
#else
    shared float sRayDistance[DDGI_BLEND_RAYS_PER_CHUNK];
#endif
shared vec3 sRayDirection[DDGI_BLEND_RAYS_PER_CHUNK];



// Bindless texture arrays (set 3, binding 0)
layout (set=3, binding = 0) uniform sampler2DArray gTextureArrays[];
layout (set=3, binding = 0) uniform usampler2DArray gUintTextureArrays[];

// Storage image bindings for writing current probe data
// coherent: the border texel pass reads interior texels written by other invocations of the same workgroup
#ifdef DDGI_BLEND_RADIANCE
    layout (set=4, binding = 1, rgba16f) uniform restrict coherent image2DArray ProbeIrradianceAtlas;
#else
    layout (set=4, binding = 2, rg16f) uniform restrict coherent image2DArray ProbeDistanceAtlas;
#endif

// Skybox Cubemap

precision highp float;

// Push constant - matching C++ DynamicDiffuseGI::DDGIBlendPushConstants
layout(push_constant) uniform PushConstants {
    uint prevTextureIndex; // will be irradiance or distance based on the blend type
    uint rayDataIndex;
    uint probeClassificationHandle;
    uint volumeSlot;
} pc;

#include "ProbeCommon.glsl"

//#define DDGI_BLEND_RADIANCE
//#define DDGI_BLEND_DISTANCE

// Input Uniforms / Buffers
// Contains global information about the probe grid
layout(std140, set=0, binding = 5) uniform ProbeInfo {
    ProbeVolume volume;
} u_probeInfo[];

ProbeVolume u_volume;



float LinearRGBToLuminance(vec3 rgb)
{
    const vec3 LuminanceWeights = vec3(0.2126, 0.7152, 0.0722);
    return dot(rgb, LuminanceWeights);
}

vec3 powVec3(vec3 value, float power)
{
    return vec3(pow(value.r, power), pow(value.g, power), pow(value.b, power));
}

// Function to update border texels by copying from the nearest interior texel
void UpdateBorderTexelsGLSL(
    ivec3 globalInvocationID,
    ivec3 localInvocationID,
    ivec3 workGroupID
) {

    bool isCornerTexel = (localInvocationID.x == 0 || localInvocationID.x == (RTXGI_DDGI_PROBE_NUM_TEXELS - 1)) && (localInvocationID.y == 0 || localInvocationID.y == (RTXGI_DDGI_PROBE_NUM_TEXELS - 1));
    bool isRowTexel = (localInvocationID.x > 0 && localInvocationID.x < (RTXGI_DDGI_PROBE_NUM_TEXELS - 1));

    uvec3 copyCoordinates = uvec3(workGroupID.x * RTXGI_DDGI_PROBE_NUM_TEXELS, workGroupID.y * RTXGI_DDGI_PROBE_NUM_TEXELS, globalInvocationID.z);

    if (isCornerTexel) {
        copyCoordinates.x += localInvocationID.x > 0 ? 1 : RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS;
        copyCoordinates.y += localInvocationID.y > 0 ? 1 : RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS;
    } else if (isRowTexel) {
        copyCoordinates.x += (RTXGI_DDGI_PROBE_NUM_TEXELS - 1) - localInvocationID.x;
        copyCoordinates.y += localInvocationID.y + (localInvocationID.y > 0 ? -1 : 1);
    } else {
        copyCoordinates.x += localInvocationID.x + (localInvocationID.x > 0 ? -1 : 1);
        copyCoordinates.y += (RTXGI_DDGI_PROBE_NUM_TEXELS - 1) - localInvocationID.y;
    }

    #ifdef DDGI_BLEND_RADIANCE
        vec4 valueToCopy = imageLoad(ProbeIrradianceAtlas, ivec3(copyCoordinates));
        imageStore(ProbeIrradianceAtlas, ivec3(globalInvocationID.xyz), valueToCopy);
    #else
        vec2 valueToCopy = imageLoad(ProbeDistanceAtlas, ivec3(copyCoordinates)).rg;
        imageStore(ProbeDistanceAtlas, ivec3(globalInvocationID.xyz), vec4(valueToCopy, 0.0, 1.0));
    #endif

}

void main() {
    u_volume = u_probeInfo[pc.volumeSlot].volume;
#if defined(DDGI_BLEND_RADIANCE) || defined(DDGI_BLEND_DISTANCE)
    // Determine if the current thread is processing an INTERIOR texel
    // Border texels are at local invocation 0 or (NUM_INTERIOR_TEXELS + 1) which is (NUM_TEXELS - 1)
    bool isBorderTexel =
        ( (gl_LocalInvocationID.x == 0 || gl_LocalInvocationID.x == (RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS + 1)) ||
           (gl_LocalInvocationID.y == 0 || gl_LocalInvocationID.y == (RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS + 1)) );

    // One workgroup covers one probe, so every thread resolves the same probe and the checks below
    // are workgroup uniform. Border texels take part in the loads and the barriers, they only skip
    // the accumulation and the store.
    ivec3 probeGridCoords;
    probeGridCoords.x = int(gl_WorkGroupID.x);
    probeGridCoords.z = int(gl_WorkGroupID.y);
    probeGridCoords.y = int(gl_WorkGroupID.z);


    //int probeIndex = DDGIGetProbeIndex(probeGridCoords, u_volume);
    int probeIndex = DDGIGetProbeIndex(ivec3(gl_GlobalInvocationID.xyz), RTXGI_DDGI_PROBE_NUM_TEXELS, u_volume);

    uint numProbes = (u_volume.gridDimensions.x * u_volume.gridDimensions.y * u_volume.gridDimensions.z);

    if (probeIndex < 0 || probeIndex >= numProbes) {
        return;
    }

    uint probeState = DDGILoadProbeState(probeIndex, gUintTextureArrays[pc.probeClassificationHandle], u_volume);
    if (probeState != 0u) {
        return;
    }

    int firstRayIndex = 0;
    ivec3 threadCoords = ivec3(gl_WorkGroupID.x * RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS, gl_WorkGroupID.y * RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS, int(gl_GlobalInvocationID.z)) + ivec3(gl_LocalInvocationID.xyz) - ivec3(1, 1, 0);

    if (u_volume.probeClassificationEnabled > 0.0 || u_volume.probeRelocationEnabled > 0.0) {
        firstRayIndex = int(u_volume.probeStaticRayCount);
    }

    vec2 probeOctantUV = DDGIGetNormalizedOctahedralCoordinates(threadCoords.xy, RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS);
    vec3 probeRayDirection = DDGIGetOctahedralDirection(probeOctantUV);

    #ifdef DDGI_BLEND_RADIANCE
        uint backfaces = 0;
        uint maxBackfaces = uint((u_volume.probeNumRays - firstRayIndex) * u_volume.probeRandomRayBackfaceThreshold);
    #endif
    #ifdef DDGI_BLEND_DISTANCE
        float probeMaxRayDistance = length(u_volume.spacing) * 1.5;
    #endif

    // Set when the backface threshold is exceeded, the probe then keeps its previous value.
    // The count is texel independent, so this resolves identically in every thread of the workgroup
    bool skipStore = false;
    vec4 result = vec4(0.0, 0.0, 0.0, 0.0);

    // The chunk bounds are workgroup uniform, so every thread runs the same number of iterations
    // and reaches the same barriers
    for (int chunkStart = firstRayIndex; chunkStart < u_volume.probeNumRays; chunkStart += DDGI_BLEND_RAYS_PER_CHUNK)
    {
        // Wait for the previous iteration to finish reading before overwriting the cache
        barrier();

        int loadRayIndex = chunkStart + int(gl_LocalInvocationIndex);
        if (loadRayIndex < u_volume.probeNumRays)
        {
            ivec3 rayDataTexCoords = ivec3(DDGIGetRayDataTexelCoords(loadRayIndex, probeIndex, u_volume));
            vec4 probeRayData = texelFetch(gTextureArrays[pc.rayDataIndex], rayDataTexCoords, 0);

            sRayDirection[gl_LocalInvocationIndex] = DDGIGetProbeRayDirection(loadRayIndex, u_volume);
    #ifdef DDGI_BLEND_RADIANCE
            sRayData[gl_LocalInvocationIndex] = probeRayData;
    #else
            sRayDistance[gl_LocalInvocationIndex] = probeRayData.a;
    #endif
        }

        barrier();

        int chunkRayCount = min(DDGI_BLEND_RAYS_PER_CHUNK, u_volume.probeNumRays - chunkStart);

        if (!isBorderTexel && !skipStore)
        {
            for (int i = 0; i < chunkRayCount; i++)
            {
                // Find the weight of the contribution for this ray
                // Weight is based on the cosine of the angle between the ray direction and the direction of the probe octant's texel
                float weight = max(0.0, dot(probeRayDirection, sRayDirection[i]));

    #ifdef DDGI_BLEND_RADIANCE
                vec3 probeRayRadiance = sRayData[i].rgb;
                float probeRayDistance = sRayData[i].a;

                // Backface hit, don't blend this sample
                if (probeRayDistance < 0.0)
                {
                    backfaces++;

                    // Only blend ray radiance into the probe if the backface threshold hasn't been exceeded
                    if (backfaces >= maxBackfaces)
                    {
                        skipStore = true;
                        break;
                    }

                    continue;
                }

                // Blend the ray's radiance
                result += vec4(probeRayRadiance * weight, weight);
    #endif

    #ifdef DDGI_BLEND_DISTANCE
                // Increase or decrease the filtered distance value's "sharpness"
                weight = pow(weight, u_volume.probeDistanceExponent);

                float probeRayDistance = min(abs(sRayDistance[i]), probeMaxRayDistance);

                result += vec4(probeRayDistance * weight, (probeRayDistance * probeRayDistance) * weight, 0.0, weight);
    #endif
            }
        }
    }

    if (!isBorderTexel && !skipStore) {

        float epsilon = float(u_volume.probeNumRays);
        epsilon -= u_volume.probeStaticRayCount;

        epsilon *= 1e-9;

        // Normalize the blended irradiance (or filtered distance), if the combined weight is not close to zero.
        // To match the Monte Carlo Estimator of Irradiance, we should divide by N (the number of radiance samples).
        // Instead, we are dividing by sum(cos(theta)) (i.e. the sum of cosine weights) to reduce variance. To account
        // for this, we must multiply in a factor of 1/2. See the Math Guide in the documentation for more information.
        // For distance, note that we are *not* dividing by the sum of the cosine weights, but to avoid branching here
         // we are still dividing by 2. This means distance values sampled from texture need to be multiplied by 2 (see
        // Irradiance.hlsl line 138).


        result.rgb *= 1.0 / (2.0 * max(result.a, epsilon));
        result.a = 1.0;


        vec3 probeIrradianceMean = vec3(0.0, 0.0, 0.0);
        #ifdef DDGI_BLEND_RADIANCE
            probeIrradianceMean = imageLoad(ProbeIrradianceAtlas, ivec3(gl_GlobalInvocationID.xyz)).rgb;
        #endif
        #ifdef DDGI_BLEND_DISTANCE
            probeIrradianceMean = imageLoad(ProbeDistanceAtlas, ivec3(gl_GlobalInvocationID.xyz)).rgb;
        #endif

        // Get the history weight (hysteresis) to use for the probe texel's previous value
        // If the probe was previously cleared to completely black, set the hysteresis to zero
        float hysteresis = u_volume.probeHysteresis;
        if (dot(probeIrradianceMean, probeIrradianceMean) == 0) hysteresis = 0.0;

    #ifdef DDGI_BLEND_RADIANCE
        // Tone-mapping gamma adjustment
        result.rgb = powVec3(result.rgb, (1.0 / u_volume.probeIrradianceEncodingGamma));

        // Get the difference between the current irradiance and the irradiance mean stored in the probe
        vec3 delta = (result.rgb - probeIrradianceMean.rgb);

        // Store the current irradiance (before interpolation) for use in probe variability
        vec3 irradianceSample = result.rgb;

        vec3 diff = probeIrradianceMean.rgb - result.rgb;
        float maxComponent = max(max(abs(diff.r), abs(diff.g)), abs(diff.b));

        float probeIrradianceThreshold = 0.5;
        if (maxComponent > probeIrradianceThreshold)
        {
            // Lower the hysteresis when a large lighting change is detected
            hysteresis = max(0.0, hysteresis - 0.5);
        }

        if (LinearRGBToLuminance(delta) > u_volume.probeBrightnessThreshold)
        {
            // Clamp the maximum per-update change in irradiance when a large brightness change is detected
            delta *= 0.25;
        }

        // Interpolate the new blended irradiance with the existing irradiance in the probe.
        // A high hysteresis value emphasizes the existing probe irradiance.
        //
        // When using lower bit depth formats for irradiance, the difference between lerped values
        // may be smaller than what the texture format can represent. This can stop progress towards
        // the target value when going from high to low values. When darkening, step at least the minimum
        // value the texture format can represent to ensure the target value is reached. The threshold value
        // for 10-bit/channel formats is always used (even for 32-bit/channel formats) to speed up light to
        // dark convergence.
        const float c_threshold = 1.0 / 1024.0;
        vec3 lerpDelta = (1.0 - hysteresis) * delta;

        float maxResultComponent = max(result.r, max(result.g, result.b));
        float maxMeanComponent = max(probeIrradianceMean.r, max(probeIrradianceMean.g, probeIrradianceMean.b));

        if (maxResultComponent < maxMeanComponent)
        {
            lerpDelta = min(max(vec3(c_threshold), abs(lerpDelta)), abs(delta)) * sign(lerpDelta);
        }

        result = vec4(probeIrradianceMean.rgb + lerpDelta, 1.0);

    #else
        result = vec4(mix(result.rg, probeIrradianceMean.rg, hysteresis), 0.0, 1.0);

    #endif


    #ifdef DDGI_BLEND_DISTANCE
        imageStore(ProbeDistanceAtlas, ivec3(gl_GlobalInvocationID.xyz), result);
    #endif
    #ifdef DDGI_BLEND_RADIANCE
        imageStore(ProbeIrradianceAtlas, ivec3(gl_GlobalInvocationID.xyz), result);
    #endif

    } // End of interior texel processing

    // Synchronization: Ensure all interior texel calculations and stores are complete
    // before any thread attempts to read them for border updates.
    memoryBarrierImage();
    barrier();

    // Border Texel Update Logic:
    if (isBorderTexel) {
        UpdateBorderTexelsGLSL(ivec3(gl_GlobalInvocationID), ivec3(gl_LocalInvocationID), ivec3(gl_WorkGroupID));
    }

#else // issue with compilation, use either DDGI_BLEND_RADIANCE or DDGI_BLEND_DISTANCE
    #ifdef DDGI_BLEND_RADIANCE
        imageStore(ProbeIrradianceAtlas, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0, 0.0, 1.0, 1.0));
    #else
        imageStore(ProbeDistanceAtlas, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0, 0.0, 1.0, 1.0));
    #endif

#endif

}



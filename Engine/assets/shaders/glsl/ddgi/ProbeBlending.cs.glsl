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



// Bindless texture arrays (set 3, binding 0)
layout (set=3, binding = 0) uniform sampler2DArray RayData[];

// Storage image bindings for writing current probe data
#ifdef DDGI_BLEND_RADIANCE
    layout (set=3, binding = 4, r11f_g11f_b10f) uniform restrict image2DArray ProbeIrradianceAtlas;        // Primary irradiance storage
    layout (set=3, binding = 5, r11f_g11f_b10f) uniform restrict image2DArray ProbeIrradianceAtlasAlt;     // Alternate irradiance storage
#else
    layout (set=3, binding = 6, rg16f) uniform restrict image2DArray ProbeDistanceAtlas;        // Primary distance storage
    layout (set=3, binding = 7, rg16f) uniform restrict image2DArray ProbeDistanceAtlasAlt;     // Alternate distance storage
#endif

// Skybox Cubemap

precision highp float;

// Push constant - matching C++ DynamicDiffuseGI::DDGIBlendPushConstants
layout(push_constant) uniform PushConstants {
    uint prevTextureIndex; // will be irradiance or distance based on the blend type
    uint rayDataIndex;
    uint writeToAlternateTexture; // 0 = write to primary, 1 = write to alternate
} pc;

#include "ProbeCommon.glsl"

//#define DDGI_BLEND_RADIANCE
//#define DDGI_BLEND_DISTANCE

// Input Uniforms / Buffers
// Contains global information about the probe grid
layout(std140, set=0, binding = 5) uniform ProbeInfo {
    ProbeVolume u_volume;
};



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
        vec4 valueToCopy;
        if (pc.writeToAlternateTexture == 0) {
            valueToCopy = imageLoad(ProbeIrradianceAtlas, ivec3(copyCoordinates));
            imageStore(ProbeIrradianceAtlas, ivec3(globalInvocationID.xyz), valueToCopy);
        } else {
            valueToCopy = imageLoad(ProbeIrradianceAtlasAlt, ivec3(copyCoordinates));
            imageStore(ProbeIrradianceAtlasAlt, ivec3(globalInvocationID.xyz), valueToCopy);
        }
    #else
        vec2 valueToCopy;
        if (pc.writeToAlternateTexture == 0) {
            valueToCopy = imageLoad(ProbeDistanceAtlas, ivec3(copyCoordinates)).rg;
            imageStore(ProbeDistanceAtlas, ivec3(globalInvocationID.xyz), vec4(valueToCopy, 0.0, 1.0));
        } else {
            valueToCopy = imageLoad(ProbeDistanceAtlasAlt, ivec3(copyCoordinates)).rg;
            imageStore(ProbeDistanceAtlasAlt, ivec3(globalInvocationID.xyz), vec4(valueToCopy, 0.0, 1.0));
        }
    #endif

}

void main() {
#if defined(DDGI_BLEND_RADIANCE) || defined(DDGI_BLEND_DISTANCE)
    // Determine if the current thread is processing an INTERIOR texel
    // Border texels are at local invocation 0 or (NUM_INTERIOR_TEXELS + 1) which is (NUM_TEXELS - 1)
    bool isBorderTexel = 
        ( (gl_LocalInvocationID.x == 0 || gl_LocalInvocationID.x == (RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS + 1)) ||
           (gl_LocalInvocationID.y == 0 || gl_LocalInvocationID.y == (RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS + 1)) );

    if (!isBorderTexel) {
        ivec3 probeGridCoords;
        probeGridCoords.x = int(gl_WorkGroupID.x);
        probeGridCoords.z = int(gl_WorkGroupID.y);
        probeGridCoords.y = int(gl_WorkGroupID.z);


        //int probeIndex = DDGIGetProbeIndex(probeGridCoords, u_volume);
        int probeIndex = DDGIGetProbeIndex(ivec3(gl_GlobalInvocationID.xyz), RTXGI_DDGI_PROBE_NUM_TEXELS, u_volume);

        uint numProbes = (u_volume.gridDimensions.x * u_volume.gridDimensions.y * u_volume.gridDimensions.z);

        if (probeIndex < 0 || probeIndex >= numProbes) return;


        ivec3 threadCoords = ivec3(gl_WorkGroupID.x * RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS, gl_WorkGroupID.y * RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS, int(gl_GlobalInvocationID.z)) + ivec3(gl_LocalInvocationID.xyz) - ivec3(1, 1, 0);




        int rayIndex = 0;
        
    #ifdef DDGI_BLEND_RADIANCE
        uint backfaces = 0;
        uint maxBackfaces = uint((u_volume.probeNumRays - rayIndex) * u_volume.probeFixedRayBackfaceThreshold);
        
        vec2 probeOctantUV = DDGIGetNormalizedOctahedralCoordinates(threadCoords.xy, RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS);
        vec3 probeRayDirection = DDGIGetOctahedralDirection(probeOctantUV);
        
    #endif
    #ifdef DDGI_BLEND_DISTANCE 

        vec2 probeOctantUV = DDGIGetNormalizedOctahedralCoordinates(threadCoords.xy, RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS);
        vec3 probeRayDirection = DDGIGetOctahedralDirection(probeOctantUV);

    #endif
        vec4 result = vec4(0.0, 0.0, 0.0, 0.0);

        for ( ; rayIndex < u_volume.probeNumRays; rayIndex++)
        {
            vec3 rayDirection = DDGIGetProbeRayDirection(rayIndex, u_volume);

            // Find the weight of the contribution for this ray
            // Weight is based on the cosine of the angle between the ray direction and the direction of the probe octant's texel
            float weight = max(0.0, dot(probeRayDirection, rayDirection));
            ivec3 rayDataTexCoords = ivec3(DDGIGetRayDataTexelCoords(rayIndex, probeIndex, u_volume));

            // Load the ray traced radiance and hit distance
            // Use texelFetch for integer coordinates, not texture()
            vec4 probeRayData = texelFetch(RayData[pc.rayDataIndex], rayDataTexCoords, 0);

            vec3 probeRayRadiance = probeRayData.rgb;
            float probeRayDistance = probeRayData.a;



    #ifdef DDGI_BLEND_RADIANCE
            // Backface hit, don't blend this sample
            if (probeRayDistance < 0.0)
            {
                backfaces++;

                // Early out: only blend ray radiance into the probe if the backface threshold hasn't been exceeded
                if (backfaces >= maxBackfaces) return;
                
                continue;
            }

            // Blend the ray's radiance
            result += vec4(probeRayRadiance * weight, weight);

    #endif

    #ifdef DDGI_BLEND_DISTANCE

            float probeMaxRayDistance = length(u_volume.spacing) * 1.5;

            // Increase or decrease the filtered distance value's "sharpness"
            weight = pow(weight, u_volume.probeDistanceExponent);

            probeRayDistance = min(abs(probeRayDistance), probeMaxRayDistance);

            result += vec4(probeRayDistance * weight, (probeRayDistance * probeRayDistance) * weight, 0.0, weight);
    #endif
        }

        float epsilon = float(u_volume.probeNumRays);

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

        #ifdef DDGI_BLEND_RADIANCE
            // Use bindless texture array to access previous irradiance texture
            vec3 probeIrradianceMean = texelFetch(RayData[pc.prevTextureIndex], ivec3(gl_GlobalInvocationID.xyz), 0).rgb;
        #endif
        #ifdef DDGI_BLEND_DISTANCE
            // Use bindless texture array to access previous distance texture
            vec2 prevDistanceData = texelFetch(RayData[pc.prevTextureIndex], ivec3(gl_GlobalInvocationID.xyz), 0).rg;
            // Store it in probeIrradianceMean for now to minimize changes to subsequent logic that uses .rg
            // However, it's clearer to use a separate variable if more changes are made later.
            vec3 probeIrradianceMean = vec3(prevDistanceData.r, prevDistanceData.g, 0.0);
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
        vec3 delta2 = (probeIrradianceMean.rgb - result.rgb);

        // Store the current irradiance (before interpolation) for use in probe variability
        vec3 irradianceSample = result.rgb;

        float maxComponent = max(max(abs(delta2.r), abs(delta2.g)), abs(delta2.b));

        float probeIrradianceThreshold = 0.25;
        if (maxComponent > probeIrradianceThreshold)
        {
            // Lower the hysteresis when a large lighting change is detected
            hysteresis = max(0.0, hysteresis - 0.75);
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
        
        maxComponent = max(result.r, max(result.g, result.b));
        float maxComponent2 = max(probeIrradianceMean.r, max(probeIrradianceMean.g, probeIrradianceMean.b));
        
        if (maxComponent < maxComponent2)
        {
            lerpDelta = min(max(vec3(c_threshold), abs(lerpDelta)), abs(delta)) * sign(lerpDelta);
        }

        result = vec4(probeIrradianceMean.rgb + lerpDelta, 1.0);

    #else
        result = vec4(mix(result.rg, probeIrradianceMean.rg, hysteresis), 0.0, 1.0);

    #endif


    #ifdef DDGI_BLEND_DISTANCE
        if (pc.writeToAlternateTexture == 0) {
            imageStore(ProbeDistanceAtlas, ivec3(gl_GlobalInvocationID.xyz), result);
        } else {
            imageStore(ProbeDistanceAtlasAlt, ivec3(gl_GlobalInvocationID.xyz), result);
        }
    #endif
    #ifdef DDGI_BLEND_RADIANCE
        
        if (pc.writeToAlternateTexture == 0) {
            imageStore(ProbeIrradianceAtlas, ivec3(gl_GlobalInvocationID.xyz), result);
        } else {
            imageStore(ProbeIrradianceAtlasAlt, ivec3(gl_GlobalInvocationID.xyz), result);
        }
    #endif

    return;
    } // End of interior texel processing

    // Synchronization: Ensure all interior texel calculations and stores are complete
    // before any thread attempts to read them for border updates.
    memoryBarrierShared();
    memoryBarrier();
    barrier();

    // Border Texel Update Logic:
    UpdateBorderTexelsGLSL(ivec3(gl_GlobalInvocationID), ivec3(gl_LocalInvocationID), ivec3(gl_WorkGroupID));

#else // issue with compilation, use either DDGI_BLEND_RADIANCE or DDGI_BLEND_DISTANCE
    #ifdef DDGI_BLEND_RADIANCE
        if (pc.writeToAlternateTexture == 0) {
            imageStore(ProbeIrradianceAtlas, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0, 0.0, 1.0, 1.0));
        } else {
            imageStore(ProbeIrradianceAtlasAlt, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0, 0.0, 1.0, 1.0));
        }
    #else
        if (pc.writeToAlternateTexture == 0) {
            imageStore(ProbeDistanceAtlas, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0, 0.0, 1.0, 1.0));
        } else {
            imageStore(ProbeDistanceAtlasAlt, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0, 0.0, 1.0, 1.0));
        }
    #endif

#endif

}



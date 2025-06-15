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



layout (set=0, binding = 0) uniform sampler2DArray RayData;

#ifdef DDGI_BLEND_RADIANCE
    layout (set=0, binding = 1, r11f_g11f_b10f) uniform restrict image2DArray ProbeIrradianceAtlas;
    layout (set=0, binding = 2) uniform sampler2DArray prevProbeIrradianceAtlas;
#else
    layout (set=0, binding = 1, rg16f) uniform restrict image2DArray ProbeDistanceAtlas;
    layout (set=0, binding = 2) uniform sampler2DArray prevProbeDistanceAtlas;
#endif

// Skybox Cubemap

precision highp float;



#include "ProbeCommon.glsl"

//#define DDGI_BLEND_RADIANCE
//#define DDGI_BLEND_DISTANCE

// Input Uniforms / Buffers
// Contains global information about the probe grid
layout(std140, set=1, binding = 0) uniform ProbeInfo {
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
    ivec3 localInvocationID,
    ivec3 workGroupID,
    ivec3 globalInvocationID
) {
    bool isCurrentThreadBorderTexel = 
        (localInvocationID.x == 0 || localInvocationID.x == (RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS + 1)) ||
        (localInvocationID.y == 0 || localInvocationID.y == (RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS + 1));

    if (isCurrentThreadBorderTexel) {
        // Base coordinates for the start of this probe's block in the atlas (top-left of the 8x8 or 16x16 block)
        ivec3 probeBlockBaseAtlasCoords = ivec3(
            workGroupID.x * RTXGI_DDGI_PROBE_NUM_TEXELS,
            workGroupID.y * RTXGI_DDGI_PROBE_NUM_TEXELS,
            int(globalInvocationID.z) // Slice index
        );

        ivec3 copyFromAtlasCoords; // The absolute atlas coordinates to copy FROM (points to an interior texel)

        // Determine the local offset of the interior texel to copy from
        int interiorCopyLocalOffsetX;
        int interiorCopyLocalOffsetY;

        if (localInvocationID.x == 0) { // Left border
            interiorCopyLocalOffsetX = 1;
        } else if (localInvocationID.x == (RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS + 1)) { // Right border
            interiorCopyLocalOffsetX = RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS;
        } else { // Interior X-coord (for row edges, or if this logic path is mistakenly hit)
            interiorCopyLocalOffsetX = int(localInvocationID.x);
        }

        if (localInvocationID.y == 0) { // Top border
            interiorCopyLocalOffsetY = 1;
        } else if (localInvocationID.y == (RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS + 1)) { // Bottom border
            interiorCopyLocalOffsetY = RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS;
        } else { // Interior Y-coord (for column edges, or if this logic path is mistakenly hit)
            interiorCopyLocalOffsetY = int(localInvocationID.y);
        }
        
        copyFromAtlasCoords = probeBlockBaseAtlasCoords + ivec3(interiorCopyLocalOffsetX, interiorCopyLocalOffsetY, 0);

        vec4 valueToCopy;
        #if defined(DDGI_BLEND_RADIANCE)
            valueToCopy = imageLoad(ProbeIrradianceAtlas, copyFromAtlasCoords);
            imageStore(ProbeIrradianceAtlas, ivec3(globalInvocationID.xyz), valueToCopy);
        #elif defined(DDGI_BLEND_DISTANCE)
            valueToCopy = imageLoad(ProbeDistanceAtlas, copyFromAtlasCoords);
            imageStore(ProbeDistanceAtlas, ivec3(globalInvocationID.xyz), valueToCopy);
        #endif
    }
}

void main() {
#if defined(DDGI_BLEND_RADIANCE) || defined(DDGI_BLEND_DISTANCE)
    // Determine if the current thread is processing an INTERIOR texel
    // Border texels are at local invocation 0 or (NUM_INTERIOR_TEXELS + 1) which is (NUM_TEXELS - 1)
    bool isProcessingInteriorTexel = 
        !( (gl_LocalInvocationID.x == 0 || gl_LocalInvocationID.x == (RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS + 1)) ||
           (gl_LocalInvocationID.y == 0 || gl_LocalInvocationID.y == (RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS + 1)) );

    if (isProcessingInteriorTexel) {
        ivec3 probeGridCoords;
        probeGridCoords.x = int(gl_WorkGroupID.x);
        probeGridCoords.z = int(gl_WorkGroupID.y);
        probeGridCoords.y = int(gl_WorkGroupID.z);


        int probeIndex = DDGIGetProbeIndex(probeGridCoords, u_volume);

        uint numProbes = (u_volume.gridDimensions.x * u_volume.gridDimensions.y * u_volume.gridDimensions.z);

        if (probeIndex < 0 || probeIndex >= numProbes) return;


        //ivec3 threadCoords = ivec3(gl_WorkGroupID.x * RTXGI_DDGI_PROBE_NUM_TEXELS, gl_WorkGroupID.y * RTXGI_DDGI_PROBE_NUM_TEXELS, int(gl_GlobalInvocationID.z)) + ivec3(gl_LocalInvocationID.xyz) - ivec3(1, 1, 0);




        int rayIndex = 0;
        
    #ifdef DDGI_BLEND_RADIANCE
        uint backfaces = 0;
        uint maxBackfaces = uint((u_volume.probeNumRays - rayIndex) * u_volume.probeFixedRayBackfaceThreshold);
        
        // Fix Y-axis inversion for Vulkan coordinate system
        ivec2 texCoords = ivec2(gl_LocalInvocationID.x, (RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS - 1) - gl_LocalInvocationID.y);
        vec2 probeOctantUV = DDGIGetNormalizedOctahedralCoordinates(ivec2(gl_LocalInvocationID.xy), u_volume.probeNumIrradianceInteriorTexels);
        vec3 probeRayDirection = DDGIGetOctahedralDirection(probeOctantUV);
        
    #endif
    #ifdef DDGI_BLEND_DISTANCE

        // Fix Y-axis inversion for Vulkan coordinate system
        ivec2 texCoords = ivec2(gl_LocalInvocationID.x, (RTXGI_DDGI_PROBE_NUM_INTERIOR_TEXELS - 1) - gl_LocalInvocationID.y);
        vec2 probeOctantUV = DDGIGetNormalizedOctahedralCoordinates(ivec2(gl_LocalInvocationID.xy), u_volume.probeNumDistanceInteriorTexels);
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
            vec4 probeRayData = texelFetch(RayData, rayDataTexCoords, 0);

            vec3 probeRayRadiance = probeRayData.rgb;
            float probeRayDistance = probeRayData.a;



    #ifdef DDGI_BLEND_RADIANCE
            // Backface hit, don't blend this sample
            if (probeRayDistance < 0.0)
            {
                backfaces++;

                // Early out: only blend ray radiance into the probe if the backface threshold hasn't been exceeded
                if (backfaces >= maxBackfaces) {
                    imageStore(ProbeIrradianceAtlas, ivec3(gl_GlobalInvocationID.xyz), vec4(-1.0, -1.0, -1.0, -1.0)); // Store BLACK
                    return;
                }
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
            // Use texelFetch for integer coordinates, not texture()
            vec3 probeIrradianceMean = texelFetch(prevProbeIrradianceAtlas, ivec3(gl_GlobalInvocationID.xyz), 0).rgb;
        #endif
        #ifdef DDGI_BLEND_DISTANCE
            // For distance blending, load history from the previous distance atlas
            // Use texelFetch for integer coordinates, not texture()
            vec2 prevDistanceData = texelFetch(prevProbeDistanceAtlas, ivec3(gl_GlobalInvocationID.xyz), 0).rg;
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

        // Store the current irradiance (before interpolation) for use in probe variability
        vec3 irradianceSample = result.rgb;

        float maxComponent = max(max(abs(delta.r), abs(delta.g)), abs(delta.b));

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


    #endif

    #ifdef DDGI_BLEND_DISTANCE
        result = vec4(mix(result.rg, probeIrradianceMean.rg, hysteresis), 0.0, 1.0);
    #endif

    #ifdef DDGI_BLEND_DISTANCE
        imageStore(ProbeDistanceAtlas, ivec3(gl_GlobalInvocationID.xyz), result);
    #endif
    #ifdef DDGI_BLEND_RADIANCE
        imageStore(ProbeIrradianceAtlas, ivec3(gl_GlobalInvocationID.xyz), result);
    #endif


    return;
    } // End of interior texel processing

    // Synchronization: Ensure all interior texel calculations and stores are complete
    // before any thread attempts to read them for border updates.
    barrier();
    
    // Add memory barrier to ensure image writes are visible
    memoryBarrierImage();

    // Border Texel Update Logic:
    UpdateBorderTexelsGLSL(ivec3(gl_LocalInvocationID), ivec3(gl_WorkGroupID), ivec3(gl_GlobalInvocationID));

#else
    imageStore(ProbeIrradianceAtlas, ivec3(gl_GlobalInvocationID.xyz), vec4(1.0, 0.0, 1.0, 1.0));

#endif

}



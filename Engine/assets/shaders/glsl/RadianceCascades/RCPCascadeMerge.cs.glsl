#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Merge cascadeLevel + 1 into cascadeLevel
// we start at n-1, because cascade n has no previous cascade
// we do this until we reach cascade 0
// cascade 0 will then contain the final result

layout(set = 4, binding = 0, rgba32f) uniform restrict image2DArray cascadeN;

layout(set = 3, binding = 0) uniform sampler2DArray gTextureArrays[];

// Push constants (updated structure)
layout(push_constant) uniform PushConstants {
    uint u_prevCascadeIndex; // index of cascade n+1, index into gTextureArrays
    uint u_currentCascadeIndex; // index of current cascade (n)
};

#include "RCCommon.glsl"


layout(std140, set = 0, binding = 7) uniform CascadeLevelInfos {
    CascadeLevelInfo cascadeLevelInfo;
} cs[];

/**
 * Spherical Fibonacci sequence for consistent ray directions
 */
vec3 SphericalFibonacci(uint sampleIndex, uint numSamples)
{
    const float b = (sqrt(5.0) * 0.5 + 0.5) - 1.0;
    float phi = 6.28318530718 * fract(float(sampleIndex) * b);
    float cosTheta = 1.0 - (2.0 * float(sampleIndex) + 1.0) * (1.0 / float(numSamples));
    float sinTheta = sqrt(clamp(1.0 - (cosTheta * cosTheta), 0.0, 1.0));

    return vec3((cos(phi) * sinTheta), (sin(phi) * sinTheta), cosTheta);
}

/**
 * Get ray direction for a specific ray index in a cascade
 */
vec3 GetProbeRayDirection(uint rayIndex, uint angularResolution)
{
    uint numRays = angularResolution * angularResolution;
    vec3 direction = SphericalFibonacci(rayIndex, numRays);
    return normalize(direction);
}





/**
 * Mathematical ray index remapping using spherical coordinates
 * Maps a ray direction from current cascade to the closest ray index in previous cascade
 */
uint RemapRayIndexMathematical(vec3 currentRayDirection, uint prevAngularResolution)
{
    uint numPrevRays = prevAngularResolution * prevAngularResolution;
    
    // Convert direction to spherical coordinates
    float phi = atan(currentRayDirection.y, currentRayDirection.x);
    float cosTheta = currentRayDirection.z;
    
    // Normalize phi to [0, 2π]
    if (phi < 0.0) phi += 6.28318530718;
    
    // Convert to spherical Fibonacci parameters
    const float b = (sqrt(5.0) * 0.5 + 0.5) - 1.0;
    
    // Solve for sampleIndex using the inverse of SphericalFibonacci
    // phi = 2π * fract(sampleIndex * b)
    // cosTheta = 1 - (2 * sampleIndex + 1) / numSamples
    
    // From cosTheta equation: sampleIndex = (1 - cosTheta) * numSamples / 2 - 0.5
    float sampleIndex = (1.0 - cosTheta) * float(numPrevRays) * 0.5 - 0.5;
    
    // Clamp to valid range
    sampleIndex = clamp(sampleIndex, 0.0, float(numPrevRays - 1));
    
    return uint(sampleIndex);
}

ivec3 getProbeCoords(uvec3 outputCoords, uint angularResolution) {
    ivec3 probeCoords;
    probeCoords.x = int(outputCoords.x / angularResolution);
    probeCoords.z = int(outputCoords.y / angularResolution);
    probeCoords.y = int(outputCoords.z);

    return probeCoords;
}

uint getRayIndex(uvec3 outputCoords, uint angularResolution) {
    uint rayIndexX = outputCoords.x % angularResolution;
    uint rayIndexY = outputCoords.y % angularResolution;
    uint rayIndex = rayIndexY * angularResolution + rayIndexX;

    return rayIndex;
}


void main() {
    // previous cascade has a lower probe count(spatial resolution)
    // so for every probe in the current cascade, we only need to find 1 probe in the previous cascade
    // this would changeto something like 8 probes if going from 0->n
    CascadeLevelInfo currentCascadeInfo = cs[u_currentCascadeIndex].cascadeLevelInfo; // cascada A
    CascadeLevelInfo prevCascadeInfo = cs[u_prevCascadeIndex].cascadeLevelInfo; // cascade B
    
    // get prev cascade' closest probe index given the current probe
    // because we work with a consisten scale the closest probe will be in the same quadrant as the current probe
    // so we can take the normalised texcoord for the probe, then get probe in cascade B using those coordinates

    uvec3 outputCoords = uvec3(gl_GlobalInvocationID.xyz);

    // The size of the output texture array
    uvec3 imageSize = uvec3(imageSize(cascadeN));

    // Discard threads that are outside of the image dimensions.
    // This can happen due to workgroup rounding.
    if (outputCoords.x >= imageSize.x || 
        outputCoords.y >= imageSize.y || 
        outputCoords.z >= imageSize.z) {
        return;
    }

    ivec3 probeCoords = getProbeCoords(outputCoords, currentCascadeInfo.angularResolution);
    uint rayIndex = getRayIndex(outputCoords, currentCascadeInfo.angularResolution);

    vec3 normalizedProbeCoords = vec3(probeCoords) / vec3(currentCascadeInfo.probeGridDimensions);
    ivec3 prevProbeCoords = ivec3((normalizedProbeCoords * vec3(prevCascadeInfo.probeGridDimensions)));
    prevProbeCoords = clamp(prevProbeCoords, ivec3(0), prevCascadeInfo.probeGridDimensions - 1);

    vec3 probeRayDirection = GetProbeRayDirection(rayIndex, currentCascadeInfo.angularResolution);

    // get the direction closest to the current ray we are processing from cascade A
    // alternativly we could interpolate between the closest probes

    uint prevRayIndex = RemapRayIndexMathematical(probeRayDirection, prevCascadeInfo.angularResolution);

    int rayX = int(prevRayIndex % prevCascadeInfo.angularResolution);
    int rayY = int(prevRayIndex / prevCascadeInfo.angularResolution);

    ivec3 texCoords;
    texCoords.x = prevProbeCoords.x * int(prevCascadeInfo.angularResolution) + rayX;
    texCoords.y = prevProbeCoords.z * int(prevCascadeInfo.angularResolution) + rayY;
    texCoords.z = prevProbeCoords.y;

    
    vec4 prevProbeData = texelFetch(gTextureArrays[prevCascadeInfo.cascadeTextureIndex], texCoords, 0);
    vec4 currentProbeData = imageLoad(cascadeN, ivec3(outputCoords));

    vec3 currentRadiance = currentProbeData.rgb;
    float currentHitT = currentProbeData.a;
    
    vec3 prevRadiance = prevProbeData.rgb;
    float prevHitT = prevProbeData.a;

    // Proper cascade merging logic
    vec3 finalRadiance;
    float finalHitT;

    // Current cascade hit a front face
    if (currentHitT > 0.0) {
        finalRadiance = currentRadiance;
        finalHitT = currentHitT;
        
        // If the previous cascade has a valid hit beyond our current hit,
        // blend it in with distance-based attenuation
        if (prevHitT > 0.0 && prevHitT > currentHitT) {
            float distanceWeight = exp(-abs(prevHitT - currentHitT) / currentCascadeInfo.maxProbeDistance);
            finalRadiance += prevRadiance * distanceWeight * 0.1; // Small contribution
        }
    }
    // Current cascade hit a back face (negative hitT)
    else if (currentHitT < 0.0) {
        // Use previous cascade data if available
        if (prevHitT > 0.0) {
            finalRadiance = prevRadiance;
            finalHitT = prevHitT;
        } else {
            // Both cascades hit back faces or missed - keep current
            finalRadiance = currentRadiance;
            finalHitT = currentHitT;
        }
    }
    // Current cascade missed (hitT == 0 or very large)
    else {
        // Use previous cascade if it has valid data
        if (prevHitT > 0.0) {
            finalRadiance = prevRadiance;
            finalHitT = prevHitT;
        } else {
            // Both missed - use current (likely skybox)
            finalRadiance = currentRadiance;
            finalHitT = currentHitT;
        }
    }

    imageStore(cascadeN, ivec3(outputCoords), vec4(finalRadiance, finalHitT));
}
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

float mad(float a, float b, float c) {
    return a * b + c;
}

float rcp(float x) {
    return 1.0 / x;
}

#define madfrac(A,B) mad((A),(B),-floor((A)*(B)))

// https://dl.acm.org/doi/10.1145/2816795.2818131
// this is out of my league, just copied it from the paper
float inverseSphericalFibonacci(vec3 direction, uint numSamples) {
    const float PHI = (1.0 + sqrt(5.0)) / 2.0;
    const float PI = 3.14159265359;
    
    // Normalize direction vector
    direction = normalize(direction);

    float phi = atan(direction.y, direction.x); // Correct range [-PI, PI]
    float cosTheta = direction.z;

    // Handle potential floating point inaccuracies at the poles
    if (abs(cosTheta) > 1.0) cosTheta = sign(cosTheta);

    // Estimate k
    float k = max(2.0, floor(log(float(numSamples) * PI * sqrt(5.0) * (1.0 - cosTheta * cosTheta)) / (2.0 * log(PHI))));

    // Calculate Fibonacci numbers F_k and F_{k+1}
    float Fk = round(pow(PHI, k) / sqrt(5.0));
    float Fk1 = round(pow(PHI, k + 1.0) / sqrt(5.0));

    // Define the basis vectors for the local coordinate system
    vec2 b1 = vec2(2.0 * PI * (fract((Fk + 1.0) * (PHI - 1.0)) - (PHI - 1.0)), -2.0 * Fk / float(numSamples));
    vec2 b2 = vec2(2.0 * PI * (fract((Fk1 + 1.0) * (PHI - 1.0)) - (PHI - 1.0)), -2.0 * Fk1 / float(numSamples));
    mat2 B = mat2(b1, b2);
    mat2 invB = inverse(B);

    // Calculate the integer coordinates in the local system
    vec2 c = floor(invB * vec2(phi, cosTheta - (1.0 - 1.0 / float(numSamples))));

    float min_dist = 1.0/0.0; // Positive infinity
    float j = 0.0;

    // Check the four candidate points
    for (int s = 0; s < 4; ++s) {
        vec2 uv = vec2(s % 2, s / 2);
        vec2 p = c + uv;

        // Reconstruct index i from local coordinates
        float i = floor(Fk * p.x + Fk1 * p.y);
        
        // Skip invalid indices
        if (i < 0.0 || i >= float(numSamples)) continue;

        // Reconstruct the point q on the sphere
        float newPhi = 2.0 * PI * fract(i * (PHI - 1.0));
        float newCosTheta = 1.0 - (2.0 * i + 1.0) / float(numSamples);
        float newSinTheta = sqrt(1.0 - newCosTheta * newCosTheta);
        vec3 q = vec3(cos(newPhi) * newSinTheta, sin(newPhi) * newSinTheta, newCosTheta);

        // Find the point with the minimum distance
        float dist = distance(q, direction);
        if (dist < min_dist) {
            min_dist = dist;
            j = i;
        }
    }
    return j;
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

vec4 getPrevCascadeData(vec3 normalizedProbeCoords, vec3 probeRayDirection, vec3 currentProbeWorldPosition, CascadeLevelInfo prevCascadeInfo) {

    // Calculate the floating-point coordinates of the current probe within the previous cascade's grid
    vec3 prevCascadeFloatCoords = normalizedProbeCoords * vec3(prevCascadeInfo.probeGridDimensions);

    // Get the base integer coordinates and the fractional part for interpolation
    ivec3 baseProbeCoords = ivec3(floor(prevCascadeFloatCoords));
    vec3 fractionalOffset = fract(prevCascadeFloatCoords);

    // Find the index and 2D coordinates of the ray direction closest to the probe's ray
    float prevRayIndex = inverseSphericalFibonacci(probeRayDirection, prevCascadeInfo.angularResolution * prevCascadeInfo.angularResolution);
    prevRayIndex = clamp(prevRayIndex, 0.0, float(prevCascadeInfo.angularResolution * prevCascadeInfo.angularResolution - 1));
    int centralRayX = int(uint(prevRayIndex) % prevCascadeInfo.angularResolution);
    int centralRayY = int(uint(prevRayIndex) / prevCascadeInfo.angularResolution);

    vec4 weightedData = vec4(0.0);

    // Trilinearly interpolate between the 8 nearest probes
    for (int x = 0; x <= 1; x++) {
        for (int y = 0; y <= 1; y++) {
            for (int z = 0; z <= 1; z++) {
                ivec3 offset = ivec3(x, y, z);
                ivec3 probeCoords = baseProbeCoords + offset;

                if (probeCoords.x < 0 || probeCoords.x >= prevCascadeInfo.probeGridDimensions.x ||
                    probeCoords.y < 0 || probeCoords.y >= prevCascadeInfo.probeGridDimensions.y ||
                    probeCoords.z < 0 || probeCoords.z >= prevCascadeInfo.probeGridDimensions.z) {
                    continue;
                 }  


                ivec3 texCoords;
                texCoords.x = probeCoords.x * int(prevCascadeInfo.angularResolution) + centralRayX;
                texCoords.y = probeCoords.z * int(prevCascadeInfo.angularResolution) + centralRayY;
                texCoords.z = probeCoords.y;
                
                vec4 prevProbeData = texelFetch(gTextureArrays[prevCascadeInfo.cascadeTextureIndex], texCoords, 0);

                // Calculate trilinear interpolation weights
                vec3 weight3d = mix(vec3(1.0) - fractionalOffset, fractionalOffset, vec3(offset));
                float weight = weight3d.x * weight3d.y * weight3d.z;

                weightedData += prevProbeData * weight;
            }
        }
    }

    return weightedData;
}

void main() {
    CascadeLevelInfo currentCascadeInfo = cs[u_currentCascadeIndex].cascadeLevelInfo;
    CascadeLevelInfo prevCascadeInfo = cs[u_prevCascadeIndex].cascadeLevelInfo;
    
    uvec3 outputCoords = uvec3(gl_GlobalInvocationID.xyz);

    uvec3 imageSize = uvec3(imageSize(cascadeN));
    if (outputCoords.x >= imageSize.x || 
        outputCoords.y >= imageSize.y || 
        outputCoords.z >= imageSize.z) {
        return;
    }

    ivec3 probeCoords = getProbeCoords(outputCoords, currentCascadeInfo.angularResolution);
    uint rayIndex = getRayIndex(outputCoords, currentCascadeInfo.angularResolution);

    // Find the corresponding probe in the previous cascade
    vec3 normalizedProbeCoords = vec3(probeCoords) / vec3(currentCascadeInfo.probeGridDimensions);

    // Get the direction of the current ray
    vec3 probeRayDirection = GetProbeRayDirection(rayIndex, currentCascadeInfo.angularResolution);
    vec3 currentProbeWorldPosition = GetProbeWorldPosition(probeCoords, currentCascadeInfo);

    vec4 currentProbeData = imageLoad(cascadeN, ivec3(outputCoords));

    currentProbeData.a = 1.0 - currentProbeData.a;


    if (currentProbeData.a != 0.0) {

        vec4 prevProbeData = getPrevCascadeData(normalizedProbeCoords, probeRayDirection, currentProbeWorldPosition, prevCascadeInfo);
        prevProbeData.a = 1.0 - prevProbeData.a;

        currentProbeData += currentProbeData.a * prevProbeData;

    }


    imageStore(cascadeN, ivec3(outputCoords), vec4(currentProbeData.rgb, 1.0));
    
}
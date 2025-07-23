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
uvec4 GetCandidateRayIndices(vec3 direction, uint numSamples) {
    const float PHI = (1.0 + sqrt(5.0)) / 2.0;
    const float PI = 3.14159265359;
    
    direction = normalize(direction);

    float phi = atan(direction.y, direction.x);
    float cosTheta = direction.z;

    if (abs(cosTheta) > 1.0) cosTheta = sign(cosTheta);

    float k = max(2.0, floor(log(float(numSamples) * PI * sqrt(5.0) * (1.0 - cosTheta * cosTheta)) / (2.0 * log(PHI))));

    float Fk = round(pow(PHI, k) / sqrt(5.0));
    float Fk1 = round(pow(PHI, k + 1.0) / sqrt(5.0));

    vec2 b1 = vec2(2.0 * PI * (fract((Fk + 1.0) * (PHI - 1.0)) - (PHI - 1.0)), -2.0 * Fk / float(numSamples));
    vec2 b2 = vec2(2.0 * PI * (fract((Fk1 + 1.0) * (PHI - 1.0)) - (PHI - 1.0)), -2.0 * Fk1 / float(numSamples));
    mat2 B = mat2(b1, b2);
    mat2 invB = inverse(B);

    vec2 c = floor(invB * vec2(phi, cosTheta - (1.0 - 1.0 / float(numSamples))));

    uvec4 candidateIndices = uvec4(0);
    int count = 0;

    for (int s = 0; s < 4; ++s) {
        vec2 uv = vec2(s % 2, s / 2);
        vec2 p = c + uv;

        float i = floor(Fk * p.x + Fk1 * p.y);
        
        if (i >= 0.0 && i < float(numSamples)) {
            candidateIndices[count++] = uint(i);
        }
    }
    // Fill remaining with a dummy value if less than 4 valid candidates
    for (int s = count; s < 4; ++s) {
        candidateIndices[s] = 0; // Or some other invalid/dummy index
    }
    return candidateIndices;
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

// normalizedProbeCoords : [0, 1] probe coordinates from the grid in the current cascade
// probeRayDirection : direction of the ray from the current cascade, this is the ray we want to match in the previous cascade
// currentProbeWorldPosition : world space position of the probe in the current cascade
// prevCascadeInfo : cascade info of the previous cascade
vec4 getPrevCascadeData(vec3 normalizedProbeCoords, vec3 probeRayDirection, vec3 currentProbeWorldPosition, CascadeLevelInfo prevCascadeInfo) {

    // Calculate the floating-point coordinates of the current probe within the previous cascade's grid
    vec3 prevCascadeFloatCoords = normalizedProbeCoords * vec3(prevCascadeInfo.probeGridDimensions);

    // Get the base integer coordinates and the fractional part for interpolation
    ivec3 baseProbeCoords = ivec3(floor(prevCascadeFloatCoords));
    vec3 fractionalOffset = fract(prevCascadeFloatCoords);

    uint numPrevSamples = prevCascadeInfo.angularResolution * prevCascadeInfo.angularResolution;
    uvec4 candidateRayIndices = GetCandidateRayIndices(probeRayDirection, numPrevSamples);

    vec4 totalWeightedData = vec4(0.0);
    float totalAngularWeight = 0.0;

    // Loop through the 4 candidate ray indices
    for (int s = 0; s < 4; ++s) {
        uint candidateRayIndex = candidateRayIndices[s];

        // Skip if it's a dummy index (less than 4 valid candidates)
        if (candidateRayIndex >= numPrevSamples) continue;

        vec3 candidateDirection = SphericalFibonacci(candidateRayIndex, numPrevSamples);
        float angularWeight = max(0.0, 1.0 - distance(candidateDirection, normalize(probeRayDirection)));

        if (angularWeight == 0.0) continue;

        int candidateRayX = int(candidateRayIndex % prevCascadeInfo.angularResolution);
        int candidateRayY = int(candidateRayIndex / prevCascadeInfo.angularResolution);

        vec4 spatiallyWeightedData = vec4(0.0);
        float spatialTotalWeight = 0.0;

        // Trilinearly interpolate between the 8 nearest probes for this candidate ray
        for (int x = 0; x <= 1; x++) {
            for (int y = 0; y <= 1; y++) {
                for (int z = 0; z <= 1; z++) {
                    ivec3 offset = ivec3(x, y, z);
                    ivec3 probeCoords = baseProbeCoords + offset;

                    if (any(lessThan(probeCoords, ivec3(0)))) continue;
                    if (any(greaterThanEqual(probeCoords, prevCascadeInfo.probeGridDimensions))) continue;

                    ivec3 texCoords;
                    texCoords.x = probeCoords.x * int(prevCascadeInfo.angularResolution) + candidateRayX;
                    texCoords.y = probeCoords.z * int(prevCascadeInfo.angularResolution) + candidateRayY;
                    texCoords.z = probeCoords.y;
                    
                    vec4 prevProbeData = texelFetch(gTextureArrays[prevCascadeInfo.cascadeTextureIndex], texCoords, 0);

                    // Calculate trilinear interpolation weights
                    vec3 weight3d = mix(vec3(1.0) - fractionalOffset, fractionalOffset, vec3(offset));
                    float weight = weight3d.x * weight3d.y * weight3d.z;

                    spatiallyWeightedData += prevProbeData * weight;
                    spatialTotalWeight += weight;
                }
            }
        }

        if (spatialTotalWeight > 0.0) {
            spatiallyWeightedData /= spatialTotalWeight;
        } else {
            continue;
        }

        totalWeightedData += spatiallyWeightedData * angularWeight;
        totalAngularWeight += angularWeight;
    }

    if (totalAngularWeight > 0.0) {
        totalWeightedData /= totalAngularWeight;
    } else {
        return vec4(1.0, 0.0, 1.0, 1.0); // Red = error
    }

    return totalWeightedData;
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

    vec4 currentData = imageLoad(cascadeN, ivec3(outputCoords));


    vec4 prevData = getPrevCascadeData(normalizedProbeCoords, probeRayDirection, currentProbeWorldPosition, prevCascadeInfo);
    
    // we do the 1 minus to transition from hit to transparancy
    // so for hits, the transparancy is 0, and for misses, the transparancy is 1
    float currentTransparency = 1.0 - currentData.a;
    float prevTransparency = 1.0 - prevData.a;


    // Apply merging equation: L_{a,c} = L_{a,b} + β_{a,b} * L_{b,c}
    vec3 mergedRadiance = currentData.rgb;
    if (currentTransparency > 0.0) {  // Only add if not fully opaque
        mergedRadiance += currentTransparency * prevData.rgb;
    }

    // Compute new transparency: β_{a,c} = β_{a,b} * β_{b,c}
    float mergedTransparency = 1.0 - (currentTransparency * prevTransparency);

    // Store merged result (RGB + β)
    imageStore(cascadeN, ivec3(outputCoords), vec4(mergedRadiance, mergedTransparency));

    
}
#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require

// One thread per probe in the grid
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

// The final merged radiance data from cascade 0
layout(set = 3, binding = 0) uniform sampler2DArray u_mergedRadiance[];

// The output 3D texture that will store the integrated irradiance
layout(set = 4, binding = 0, rgba16f) uniform restrict writeonly image3D u_irradianceVolume;

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

void main() {
    // This shader processes the probes of the highest-resolution cascade (cascade 0)
    CascadeLevelInfo cascadeInfo = cs[0].cascadeLevelInfo;

    // Get the 3D coordinates of the probe this thread is responsible for
    ivec3 probeCoords = ivec3(gl_GlobalInvocationID.xyz);
    
    uvec3 gridDimensions = uvec3(cascadeInfo.probeGridDimensions);
    if (probeCoords.x >= gridDimensions.x || 
        probeCoords.y >= gridDimensions.y || 
        probeCoords.z >= gridDimensions.z) {
        return;
    }

    uint angularResolution = cascadeInfo.angularResolution;
    uint numRays = angularResolution * angularResolution;

    vec3 integratedRadiance = vec3(0.0);
    float totalWeight = 0.0;

    // Integrate incoming radiance over the hemisphere for this single probe
    for (uint rayIndex = 0; rayIndex < numRays; ++rayIndex) {
        // Reconstruct the texel coordinates to fetch the radiance data
        uint rayX = rayIndex % angularResolution;
        uint rayY = rayIndex / angularResolution;

        ivec3 texCoords;
        texCoords.x = probeCoords.x * int(angularResolution) + int(rayX);
        texCoords.y = probeCoords.z * int(angularResolution) + int(rayY);
        texCoords.z = probeCoords.y;

        // Fetch the final merged radiance for this ray
        vec4 probeData = texelFetch(u_mergedRadiance[cascadeInfo.cascadeTextureIndex], texCoords, 0);
        vec3 radiance = probeData.rgb;
        float opacity = probeData.a; // We can use this for visibility/confidence in the future

        // The weight for diffuse irradiance is 1.0 for each ray direction.
        // The Lambertian cosine term is applied on the surface during the final lighting pass.
        // Here, we are just averaging the incoming light.
        float weight = 1.0;

        integratedRadiance += radiance * weight;
        totalWeight += weight;
    }

    if (totalWeight > 0.0) {
        // Average the radiance over all rays to get the final irradiance
        integratedRadiance /= totalWeight;
    }

    // Store the final pre-filtered irradiance value in the 3D texture
    imageStore(u_irradianceVolume, probeCoords, vec4(integratedRadiance, 1.0));
}

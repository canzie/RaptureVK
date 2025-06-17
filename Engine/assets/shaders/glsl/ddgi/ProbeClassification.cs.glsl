#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

// Ray data input texture
layout (set=0, binding = 0) uniform sampler2DArray RayData;

// Probe state output buffer (R32_UINT format)
// Values: 0 = Active, 1 = Inactive (inside geometry), 2 = Needs Relocation
layout (set=0, binding = 1, r32ui) uniform restrict uimage2DArray ProbeStates;

// Probe offset data (for relocation)
layout (set=0, binding = 2, rgba32f) uniform restrict image2DArray ProbeOffsets;

precision highp float;

#include "ProbeCommon.glsl"

// Input Uniforms / Buffers
layout(std140, set=1, binding = 0) uniform ProbeInfo {
    ProbeVolume u_volume;
};

// Probe classification constants
#define PROBE_STATE_ACTIVE 0u
#define PROBE_STATE_INACTIVE 1u
#define PROBE_STATE_NEEDS_RELOCATION 2u

void main() {
    if (u_volume.probeClassificationEnabled < 0.5) {
        return; // Classification disabled
    }

    // Calculate probe index
    uint probeIndex = gl_GlobalInvocationID.x;
    uint totalProbes = u_volume.gridDimensions.x * u_volume.gridDimensions.y * u_volume.gridDimensions.z;
    
    if (probeIndex >= totalProbes) {
        return;
    }

    // Get probe coordinates
    ivec3 probeCoords = DDGIGetProbeCoords(int(probeIndex), u_volume);
    uvec3 probeTexelCoords = DDGIGetProbeTexelCoords(int(probeIndex), u_volume);
    
    // Analyze ray data for this probe
    uint validSamples = 0u;
    uint backfaceSamples = 0u;
    uint frontfaceSamples = 0u;
    float minFrontfaceDistance = 1000000.0;
    float totalDistance = 0.0;
    
    // Iterate through all rays for this probe
    for (int rayIndex = 0; rayIndex < u_volume.probeNumRays; rayIndex++) {
        uvec3 rayDataCoords = DDGIGetRayDataTexelCoords(rayIndex, int(probeIndex), u_volume);
        vec4 rayData = texelFetch(RayData, ivec3(rayDataCoords), 0);
        
        float hitDistance = rayData.a;
        vec3 radiance = rayData.rgb;
        
        if (abs(hitDistance) < u_volume.probeMaxRayDistance) {
            validSamples++;
            totalDistance += abs(hitDistance);
            
            if (hitDistance > 0.0) {
                // Frontface hit
                frontfaceSamples++;
                minFrontfaceDistance = min(minFrontfaceDistance, hitDistance);
            } else {
                // Backface hit
                backfaceSamples++;
            }
        }
    }
    
    // Determine probe state based on ray results
    uint newState = PROBE_STATE_ACTIVE;
    
    // Check if probe has enough valid samples
    if (validSamples < uint(u_volume.probeMinValidSamples)) {
        newState = PROBE_STATE_INACTIVE;
    }
    // Check backface threshold - if too many backface hits, probe is likely inside geometry
    else if (float(backfaceSamples) / float(validSamples) > u_volume.probeFixedRayBackfaceThreshold) {
        newState = PROBE_STATE_INACTIVE;
    }
    // Check if probe is too close to geometry
    else if (frontfaceSamples > 0u && minFrontfaceDistance < u_volume.probeMinFrontfaceDistance) {
        newState = PROBE_STATE_NEEDS_RELOCATION;
    }
    
    // Store the classification result
    imageStore(ProbeStates, ivec3(probeTexelCoords), uvec4(newState, 0u, 0u, 0u));
    
    // Calculate offset direction for relocation (if needed)
    if (newState == PROBE_STATE_NEEDS_RELOCATION && frontfaceSamples > 0u) {
        // Find the direction with the most clearance
        vec3 bestDirection = vec3(0.0);
        float maxDistance = 0.0;
        
        for (int rayIndex = 0; rayIndex < u_volume.probeNumRays; rayIndex++) {
            uvec3 rayDataCoords = DDGIGetRayDataTexelCoords(rayIndex, int(probeIndex), u_volume);
            vec4 rayData = texelFetch(RayData, ivec3(rayDataCoords), 0);
            
            float hitDistance = rayData.a;
            if (hitDistance > maxDistance) {
                maxDistance = hitDistance;
                bestDirection = DDGIGetProbeRayDirection(rayIndex, u_volume);
            }
        }
        
        // Store relocation offset (direction and distance)
        vec3 relocationOffset = bestDirection * min(maxDistance * 0.5, length(u_volume.spacing) * 0.25);
        imageStore(ProbeOffsets, ivec3(probeTexelCoords), vec4(relocationOffset, 1.0));
    } else {
        // No relocation needed
        imageStore(ProbeOffsets, ivec3(probeTexelCoords), vec4(0.0, 0.0, 0.0, 0.0));
    }
} 
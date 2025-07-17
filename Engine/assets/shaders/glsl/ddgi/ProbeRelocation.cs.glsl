#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require

// -----------------------------------------------------------------------------
//  DDGI Probe-relocation Compute Shader
//  Computes an updated relocation offset for every probe using the rules
//  described in the pseudo-code supplied by the user.
// -----------------------------------------------------------------------------

// One probe per invocation (choose X threads for throughput – must match dispatch)
layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

// -----------------------------------------------------------------------------
//  Resource bindings (match your engine's descriptor table)                    
// -----------------------------------------------------------------------------
// 0,0  –  Ray-data texture (RGBA32F) produced by ProbeTrace pass
layout(set = 3, binding = 0) uniform sampler2DArray RayData[];

// 0,1  –  Probe state image (optional, not used in this shader)
//layout(set = 0, binding = 1, r32ui) uniform restrict uimage2DArray ProbeStates;

// 0,2  –  Probe offset image (read-modify-write)
layout(set = 4, binding = 6, rgba32f) uniform restrict image2DArray ProbeOffsets;

// -----------------------------------------------------------------------------
//  DDGI volume parameters                                                      
// -----------------------------------------------------------------------------
#include "ProbeCommon.glsl"

// Global DDGI info (same uniform block used everywhere else)
layout(std140, set = 0, binding = 5) uniform ProbeInfo {
    ProbeVolume u_volume;
};

layout(push_constant) uniform PushConstants {
    uint rayDataIndex;
} pc;



void main() {
    // ---------------------------------------------------------------------
    // Determine probe index and texture coordinates                         
    // ---------------------------------------------------------------------
    uint probeIndex      = gl_GlobalInvocationID.x; // 1D dispatch ‑ one probe per thread
    uint totalProbes     = u_volume.gridDimensions.x * u_volume.gridDimensions.y * u_volume.gridDimensions.z;
    if (probeIndex >= totalProbes) return;

    uvec3 probeTexelCoord = DDGIGetProbeTexelCoords(int(probeIndex), u_volume);
    ivec3 texelCoord      = ivec3(probeTexelCoord);
    vec4 probeData        = imageLoad(ProbeOffsets, texelCoord);

    vec3 offset =  DDGILoadProbeDataOffset(probeData.xyz, u_volume);


    // ---------------------------------------------------------------------
    // Gather per-probe ray statistics                                       
    // ---------------------------------------------------------------------
    int   backfaceCount               = 0;
    float closestBackfaceDistance     = 1e30;
    int   closestBackfaceIndex        = -1;

    float closestFrontfaceDistance    = 1e30;
    int   closestFrontfaceIndex       = -1;

    float farthestFrontfaceDistance   = -1.0;
    int   farthestFrontfaceIndex      = -1;

    int numRays = min(u_volume.probeNumRays, int(u_volume.probeStaticRayCount));

    for (int rayIdx = 0; rayIdx < numRays; rayIdx++) {
        ivec3 rayDataTexCoords = ivec3(DDGIGetRayDataTexelCoords(rayIdx, int(probeIndex), u_volume));
        vec4  rayData  = texelFetch(RayData[pc.rayDataIndex], rayDataTexCoords, 0);

        float hitT = rayData.a;

        if (hitT < 0.0) {
            backfaceCount++;

            hitT = hitT * -0.5;
            if (hitT > closestBackfaceDistance) {
                closestBackfaceDistance = hitT;
                closestBackfaceIndex = rayIdx;
            }
        } else {
            if (hitT < closestFrontfaceDistance) {
                closestFrontfaceDistance = hitT;
                closestFrontfaceIndex = rayIdx;
            } else if (hitT > farthestFrontfaceDistance) {
                farthestFrontfaceDistance = hitT;
                farthestFrontfaceIndex = rayIdx;
            }
        }
    }

    vec3 fullOffset = vec3(1e27);

    if (closestBackfaceIndex != -1 && (float(backfaceCount) / float(numRays)) > u_volume.probeFixedRayBackfaceThreshold) {
        vec3 closestBackfaceDirection = DDGIGetProbeRayDirection(closestBackfaceIndex, u_volume);
        fullOffset = offset + (closestBackfaceDirection * (closestBackfaceDistance + u_volume.probeMinFrontfaceDistance * 0.5));
    } else if (closestFrontfaceDistance < u_volume.probeMinFrontfaceDistance) {
        // Don't move the probe if moving towards the farthest frontface will also bring us closer to the nearest frontface
        vec3 closestFrontfaceDirection = DDGIGetProbeRayDirection(closestFrontfaceIndex, u_volume);
        vec3 farthestFrontfaceDirection = DDGIGetProbeRayDirection(farthestFrontfaceIndex, u_volume);

        if (dot(closestFrontfaceDirection, farthestFrontfaceDirection) <= 0.0)
        {
            // Ensures the probe never moves through the farthest frontface
            farthestFrontfaceDirection *= min(farthestFrontfaceDistance, 1.0);
            fullOffset = offset + farthestFrontfaceDirection;
        }
    } else if (closestFrontfaceDistance > u_volume.probeMinFrontfaceDistance) {
        // Probe isn't near anything, try to move it back towards zero offset
        float moveBackMargin = min(closestFrontfaceDistance - u_volume.probeMinFrontfaceDistance, length(offset));
        vec3 moveBackDirection = normalize(-offset);
        fullOffset = offset + (moveBackMargin * moveBackDirection);
    }

    // Absolute maximum distance that probe could be moved should satisfy ellipsoid equation:
    // x^2 / probeGridSpacing.x^2 + y^2 / probeGridSpacing.y^2 + z^2 / probeGridSpacing.y^2 < (0.5)^2
    // Clamp to less than maximum distance to avoid degenerate cases
    vec3 normalizedOffset = fullOffset / u_volume.spacing;
    if (dot(normalizedOffset, normalizedOffset) < 0.2025) // 0.45 * 0.45 == 0.2025
    {
        offset = fullOffset;
    }

    // ---------------------------------------------------------------------
    // Store back                                                             
    // ---------------------------------------------------------------------
    imageStore(ProbeOffsets, texelCoord, vec4(offset, 1.0));
} 
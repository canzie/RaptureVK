#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require

// -----------------------------------------------------------------------------
//  DDGI Probe Relocation Compute Shader (GLSL port of RTXGI HLSL version)
//  One invocation = one probe relocation.
// -----------------------------------------------------------------------------

// HLSL: [numthreads(32, 1, 1)]
layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

// -----------------------------------------------------------------------------
//  Resources
// -----------------------------------------------------------------------------
// Ray data produced by `ProbeTrace.cs.glsl` (bindless array, same convention as
// `ProbeBlending.cs.glsl`)
layout(set = 3, binding = 0) uniform sampler2DArray gTextureArrays[];

// Probe world–space offset storage. Alpha channel is unused here but kept as 1.0
// for consistency with the rest of the DDGI system.
layout(set = 4, binding = 4, rgba32f) uniform restrict image2DArray ProbeOffsets;

// DDGI volume parameters (shared with other DDGI shaders)
#include "ProbeCommon.glsl"

layout(std140, set = 0, binding = 5) uniform ProbeInfo {
    ProbeVolume u_volume;
};

// Push constants: index into bindless ray-data array
layout(push_constant) uniform PushConstants {
    uint rayDataIndex;
} pc;

// -----------------------------------------------------------------------------
//  Helper: load ray distance exactly as the HLSL version expects
// -----------------------------------------------------------------------------
// In `ProbeTrace.cs.glsl` we encode:
//  - front‑face hit   -> RayData.a = +t
//  - back‑face hit    -> RayData.a = -t * 0.2  ("visibility" term)
// The original RTXGI relocation shader then does `hitDistance * -5` on
// backfaces to reconstruct the original distance (since -t*0.2 * -5 = t).
// This function mirrors that logic so the ported algorithm behaves the same.
float DDGILoadProbeRayDistance_GLSL(ivec3 rayTexelCoords) {
    vec4 data = texelFetch(gTextureArrays[pc.rayDataIndex], rayTexelCoords, 0);
    return data.a;
}

// -----------------------------------------------------------------------------
//  Entry point (GLSL port of DDGIProbeRelocationCS)
// -----------------------------------------------------------------------------
void main() {
    // Probe index for this thread (1D dispatch: X dimension only)
    uint probeIndex = gl_GlobalInvocationID.x;

    // Total number of probes in the volume
    uint numProbes = u_volume.gridDimensions.x * u_volume.gridDimensions.y * u_volume.gridDimensions.z;
    if (probeIndex >= numProbes) {
        return;
    }

    // Get the probe's texel coordinates in the ProbeOffsets texture array
    uvec3 outputCoords = DDGIGetProbeTexelCoords(int(probeIndex), u_volume);
    ivec3 texelCoord  = ivec3(outputCoords);

    // Read the current world‑space position offset for this probe
    vec3 storedOffset = imageLoad(ProbeOffsets, texelCoord).xyz;
    vec3 offset       = DDGILoadProbeDataOffset(storedOffset, u_volume);

    // ---------------------------------------------------------------------
    //  Gather per‑probe ray statistics
    // ---------------------------------------------------------------------
    int   closestBackfaceIndex      = -1;
    int   closestFrontfaceIndex     = -1;
    int   farthestFrontfaceIndex    = -1;

    float closestBackfaceDistance   = 1e27;
    float closestFrontfaceDistance  = 1e27;
    float farthestFrontfaceDistance = 0.0;
    float backfaceCount             = 0.0;

    // Number of rays to inspect: limit to the fixed/static rays used for
    // relocation/classification stability. In our GLSL setup the fixed ray
    // count is stored in `probeStaticRayCount`.
    int numRays = min(u_volume.probeNumRays, int(u_volume.probeStaticRayCount));

    for (int rayIndex = 0; rayIndex < numRays; ++rayIndex) {
        // Get the coordinates for this probe ray in the RayData texture array
        ivec3 rayDataTexCoords = ivec3(DDGIGetRayDataTexelCoords(rayIndex, int(probeIndex), u_volume));

        // Load the hit distance for the ray
        float hitDistance = DDGILoadProbeRayDistance_GLSL(rayDataTexCoords);

        if (hitDistance < 0.0) {
            // Found a back‑face
            backfaceCount += 1.0;

            // Negate the hit distance on a backface hit and scale back to full distance
            hitDistance = hitDistance * -5.0; // Undo -t * 0.2 stored in RayData

            if (hitDistance < closestBackfaceDistance) {
                closestBackfaceDistance = hitDistance;
                closestBackfaceIndex    = rayIndex;
            }
        } else {
            // Front‑face hit
            if (hitDistance < closestFrontfaceDistance) {
                closestFrontfaceDistance = hitDistance;
                closestFrontfaceIndex    = rayIndex;
            } else if (hitDistance > farthestFrontfaceDistance) {
                farthestFrontfaceDistance = hitDistance;
                farthestFrontfaceIndex    = rayIndex;
            }
        }
    }

    // ---------------------------------------------------------------------
    //  Compute relocation offset (direct port of RTXGI logic)
    // ---------------------------------------------------------------------
    vec3 fullOffset = vec3(1e27);

    if (closestBackfaceIndex != -1 && (backfaceCount / float(numRays)) > u_volume.probeFixedRayBackfaceThreshold) {
        // Enough backfaces: probe is likely inside geometry, move it outward
        vec3 closestBackfaceDirection = DDGIGetProbeRayDirection(closestBackfaceIndex, u_volume);
        fullOffset = offset + closestBackfaceDirection * (closestBackfaceDistance + u_volume.probeMinFrontfaceDistance * 0.5);

    } else if (closestFrontfaceDistance < u_volume.probeMinFrontfaceDistance) {
        // Very close to front‑face geometry: try to move towards the farthest front‑face
        vec3 closestFrontfaceDirection  = DDGIGetProbeRayDirection(closestFrontfaceIndex, u_volume);
        vec3 farthestFrontfaceDirection = DDGIGetProbeRayDirection(farthestFrontfaceIndex, u_volume);

        // Don't move the probe if moving towards the farthest front‑face would also
        // bring it closer to the nearest front‑face.
        if (dot(closestFrontfaceDirection, farthestFrontfaceDirection) <= 0.0) {
            // Ensure the probe never moves through the farthest front‑face
            farthestFrontfaceDirection *= min(farthestFrontfaceDistance, 1.0);
            fullOffset = offset + farthestFrontfaceDirection;
        }

    } else if (closestFrontfaceDistance > u_volume.probeMinFrontfaceDistance) {
        // Probe isn't near anything, try to move it back towards zero offset
        float moveBackMargin  = min(closestFrontfaceDistance - u_volume.probeMinFrontfaceDistance, length(offset));
        vec3  moveBackDirection = normalize(-offset);
        fullOffset = offset + moveBackMargin * moveBackDirection;
    }

    // ---------------------------------------------------------------------
    //  Clamp offset to lie well within the probe ellipsoid
    // ---------------------------------------------------------------------
    // Absolute maximum distance that probe could be moved should satisfy ellipsoid equation:
    // x^2 / spacing.x^2 + y^2 / spacing.y^2 + z^2 / spacing.z^2 < (0.5)^2
    // Clamp to slightly less than maximum distance (0.45) to avoid degenerate cases.
    vec3 normalizedOffset = fullOffset / u_volume.spacing;
    if (dot(normalizedOffset, normalizedOffset) < 0.2025) { // 0.45 * 0.45
        offset = fullOffset;
    }

    // ---------------------------------------------------------------------
    //  Store updated probe offset back to texture
    // ---------------------------------------------------------------------
    // Convert world‑space offset back to normalized space (per‑axis spacing)
    vec3 storedNewOffset = offset / u_volume.spacing;
    imageStore(ProbeOffsets, texelCoord, vec4(storedNewOffset, 1.0));
}

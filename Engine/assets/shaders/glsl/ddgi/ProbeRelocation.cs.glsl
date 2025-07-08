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
layout(set = 0, binding = 0) uniform sampler2DArray RayData;

// 0,1  –  Probe state image (optional, not used in this shader)
//layout(set = 0, binding = 1, r32ui) uniform restrict uimage2DArray ProbeStates;

// 0,2  –  Probe offset image (read-modify-write)
layout(set = 0, binding = 2, rgba32f) uniform restrict image2DArray ProbeOffsets;

// -----------------------------------------------------------------------------
//  DDGI volume parameters                                                      
// -----------------------------------------------------------------------------
#include "ProbeCommon.glsl"

// Global DDGI info (same uniform block used everywhere else)
layout(std140, set = 1, binding = 0) uniform ProbeInfo {
    ProbeVolume u_volume;
};

// Tweakables – you can fetch these from a UBO if you expose them in C++
const float PROBE_OFFSET_LIMIT   = 0.35;   // Fraction of spacing a probe is allowed to move
const float BACKFACE_FRACTION    = 0.25;   // 25 % backfaces → assume we are inside
const float MIN_FRONTFACE_FACTOR = 0.2;    // (m) max step towards open space when not inside
const float EPSILON              = 1e-3;   // Small bias to stay within bounds (world units)

// Convenient alias
#define RAYS_PER_PROBE  (u_volume.probeNumRays)

// ----------------------------------------------------------------------------
// Helper – safe component-wise division (returns large value when |d| < 1e-5)
// ----------------------------------------------------------------------------
vec3 safeDivide(vec3 a, vec3 b) {
    vec3 r;
    r.x = (abs(b.x) < 1e-5) ? 1e10 : a.x / b.x;
    r.y = (abs(b.y) < 1e-5) ? 1e10 : a.y / b.y;
    r.z = (abs(b.z) < 1e-5) ? 1e10 : a.z / b.z;
    return r;
}

void main() {
    // ---------------------------------------------------------------------
    // Determine probe index and texture coordinates                         
    // ---------------------------------------------------------------------
    uint probeIndex      = gl_GlobalInvocationID.x; // 1D dispatch ‑ one probe per thread
    uint totalProbes     = u_volume.gridDimensions.x * u_volume.gridDimensions.y * u_volume.gridDimensions.z;
    if (probeIndex >= totalProbes) { return; }

    ivec3 probeCoords    = DDGIGetProbeCoords(int(probeIndex), u_volume);
    uvec3 probeTexelCoord= DDGIGetProbeTexelCoords(int(probeIndex), u_volume);
    ivec3 texelCoord     = ivec3(probeTexelCoord);

    // ---------------------------------------------------------------------
    // Gather per-probe ray statistics                                       
    // ---------------------------------------------------------------------
    int   backfaceCount               = 0;
    float closestBackfaceDistance     = 1e30;
    vec3  closestBackfaceDirection    = vec3(0.0);

    float closestFrontfaceDistance    = 1e30;
    vec3  closestFrontfaceDirection   = vec3(0.0);

    float farthestFrontfaceDistance   = -1.0;
    vec3  farthestFrontfaceDirection  = vec3(0.0);

    for (int rayIdx = 0; rayIdx < RAYS_PER_PROBE; ++rayIdx) {
        uvec3 rayCoords = DDGIGetRayDataTexelCoords(rayIdx, int(probeIndex), u_volume);
        vec4  rayData   = texelFetch(RayData, ivec3(rayCoords), 0);
        float hitT      = rayData.a;
        vec3  rayDir    = DDGIGetProbeRayDirection(rayIdx, u_volume);

        if (hitT < 0.0) {
            // Back-face hit (stored as negative distance * 0.2 in the trace pass)
            backfaceCount++;
            float dist = abs(hitT);
            if (dist < closestBackfaceDistance) { closestBackfaceDistance = dist; closestBackfaceDirection = rayDir; }
        } else if (hitT > 0.0 && hitT < u_volume.probeMaxRayDistance) {
            if (hitT < closestFrontfaceDistance) { closestFrontfaceDistance = hitT; closestFrontfaceDirection = rayDir; }
            if (hitT > farthestFrontfaceDistance) { farthestFrontfaceDistance = hitT; farthestFrontfaceDirection = rayDir; }
        }
    }

    // ---------------------------------------------------------------------
    // Load current offset from atlas                                         
    // ---------------------------------------------------------------------
    vec4  offsetTexel   = imageLoad(ProbeOffsets, texelCoord);
    vec3  currentOffset = offsetTexel.xyz;

    // ---------------------------------------------------------------------
    // Compute the per-axis movement limit                                   
    // ---------------------------------------------------------------------
    vec3 offsetLimit = PROBE_OFFSET_LIMIT * u_volume.spacing;

    // Final offset we will write
    vec3 fullOffset = currentOffset; // default – don't move

    // ---------------------------------------------------------------------
    //  PRIMARY BRANCH – probe is probably inside geometry                   
    // ---------------------------------------------------------------------
    if (float(backfaceCount) / float(RAYS_PER_PROBE) > BACKFACE_FRACTION && (closestBackfaceDistance < 1e20)) {
        // Solve for the max scaling possible on each axis so we remain inside the cell bounds
        vec3 positiveOffset   = safeDivide((-currentOffset + offsetLimit),  closestBackfaceDirection);
        vec3 negativeOffset   = safeDivide((-currentOffset - offsetLimit),  closestBackfaceDirection);
        vec3 combinedOffset   = vec3(max(positiveOffset.x, negativeOffset.x),
                                     max(positiveOffset.y, negativeOffset.y),
                                     max(positiveOffset.z, negativeOffset.z));

        float scaleFactor = min(min(combinedOffset.x, combinedOffset.y), combinedOffset.z) - EPSILON;

        // If scaleFactor <= 1.0 we can't move meaningfully – stay put.
        if (scaleFactor > 1.0) {
            fullOffset = currentOffset + closestBackfaceDirection * scaleFactor;
        }
    }
    // ---------------------------------------------------------------------
    //  SECONDARY BRANCH – move slightly towards open space                  
    // ---------------------------------------------------------------------
    else if (farthestFrontfaceDistance > 0.0) {
        float moveDist = min(MIN_FRONTFACE_FACTOR, farthestFrontfaceDistance);
        fullOffset     = currentOffset + normalize(farthestFrontfaceDirection) * moveDist;
    }

    // ---------------------------------------------------------------------
    // Store back                                                             
    // ---------------------------------------------------------------------
    imageStore(ProbeOffsets, texelCoord, vec4(fullOffset, 1.0));
} 
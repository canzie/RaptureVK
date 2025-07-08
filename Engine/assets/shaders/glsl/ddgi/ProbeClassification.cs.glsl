#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require

// -----------------------------------------------------------------------------
//  DDGI Probe-classification Compute Shader                                     
//  Transitions probes between the seven logical states required by the paper:  
//  Uninitialised, Sleeping, NewlyAwake, Awake, Off, NewlyVigilant, Vigilant.   
// -----------------------------------------------------------------------------
//  One probe per work-item (match dispatch in C++)
layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

// -----------------------------------------------------------------------------
//  Resource bindings                                                            
// -----------------------------------------------------------------------------
// 0,0  – Ray data written by the ProbeTrace pass                                
layout(set = 0, binding = 0) uniform sampler2DArray RayData;

// 0,1  – Probe state image (R32_UINT)  (read-modify-write)
layout(set = 0, binding = 1, r32ui) uniform restrict uimage2DArray ProbeStates;

// -----------------------------------------------------------------------------
//  Shared DDGI helpers / structs                                                
// -----------------------------------------------------------------------------
#include "ProbeCommon.glsl"

layout(std140, set = 1, binding = 0) uniform ProbeInfo {
    ProbeVolume u_volume;
};

// -----------------------------------------------------------------------------
//  Enumerated probe states (uint encodings)                                     
// -----------------------------------------------------------------------------
const uint PROBE_STATE_UNINITIALISED   = 0u;
const uint PROBE_STATE_SLEEPING        = 1u;
const uint PROBE_STATE_NEWLY_AWAKE     = 2u;
const uint PROBE_STATE_AWAKE           = 3u;
const uint PROBE_STATE_OFF             = 4u;
const uint PROBE_STATE_NEWLY_VIGILANT  = 5u;
const uint PROBE_STATE_VIGILANT        = 6u;

// Convenient alias
#define RAYS_PER_PROBE  (u_volume.probeNumRays)

// Helper – count front / back hits and distances --------------------------------
void accumulateRayStatistics(
    in  uint  probeIndex,
    out int   backfaceCount,
    out float closestFrontDist,
    out float farthestFrontDist)
{
    backfaceCount      = 0;
    closestFrontDist   = 1e30;
    farthestFrontDist  = -1.0;

    for (int r = 0; r < RAYS_PER_PROBE; ++r) {
        uvec3 rayCoord = DDGIGetRayDataTexelCoords(r, int(probeIndex), u_volume);
        float hitT     = texelFetch(RayData, ivec3(rayCoord), 0).a;

        if (hitT < 0.0) {
            backfaceCount++;
        } else if (hitT > 0.0 && hitT < u_volume.probeMaxRayDistance) {
            closestFrontDist  = min(closestFrontDist, hitT);
            farthestFrontDist = max(farthestFrontDist, hitT);
        }
    }

    if (closestFrontDist == 1e30)  closestFrontDist  = -1.0; // no hit
    if (farthestFrontDist < 0.0)   farthestFrontDist = -1.0;
}

void main() {
    // ---------------------------------------------------------------------
    // Indexing helpers                                                      
    // ---------------------------------------------------------------------
    uint probeIndex  = gl_GlobalInvocationID.x;
    uint totalProbes = u_volume.gridDimensions.x * u_volume.gridDimensions.y * u_volume.gridDimensions.z;
    if (probeIndex >= totalProbes) { return; }

    uvec3 texelCoordU  = DDGIGetProbeTexelCoords(int(probeIndex), u_volume);
    ivec3 texelCoord   = ivec3(texelCoordU);

    // ---------------------------------------------------------------------
    // Load previous state                                                   
    // ---------------------------------------------------------------------
    uint prevState = imageLoad(ProbeStates, texelCoord).x;

    // ---------------------------------------------------------------------
    // Gather statistics from current frame rays                             
    // ---------------------------------------------------------------------
    int   backfaceCount;
    float closestFront;
    float farthestFront;
    accumulateRayStatistics(probeIndex, backfaceCount, closestFront, farthestFront);

    // Does this probe currently "touch" geometry (<= spacing distance)?
    float geomTouchDist = length(u_volume.spacing);
    bool  nearGeometry  = (closestFront > 0.0) && (closestFront < geomTouchDist);

    // ---------------------------------------------------------------------
    // State machine                                                         
    // ---------------------------------------------------------------------
    uint newState = prevState;

    switch (prevState) {
    case PROBE_STATE_UNINITIALISED:
        if (backfaceCount > int(float(RAYS_PER_PROBE) * u_volume.probeFixedRayBackfaceThreshold)) {
            newState = PROBE_STATE_OFF;
        }
        else if (nearGeometry) {
            newState = PROBE_STATE_NEWLY_VIGILANT;
        }
        else {
            newState = PROBE_STATE_SLEEPING;
        }
        break;

    case PROBE_STATE_SLEEPING:
        if (nearGeometry) {
            newState = PROBE_STATE_NEWLY_AWAKE;
        }
        break;

    case PROBE_STATE_NEWLY_AWAKE:
        // One-frame transition → Awake
        newState = PROBE_STATE_AWAKE;
        break;

    case PROBE_STATE_NEWLY_VIGILANT:
        newState = PROBE_STATE_VIGILANT;
        break;

    case PROBE_STATE_AWAKE:
        // If it no longer shades geometry, let it sleep
        if (!nearGeometry) { newState = PROBE_STATE_SLEEPING; }
        break;

    case PROBE_STATE_VIGILANT:
        // A vigilant probe only turns off if it loses all valid samples
        if (!nearGeometry && closestFront < 0.0) { newState = PROBE_STATE_OFF; }
        break;

    case PROBE_STATE_OFF:
        // If geometry appears, become vigilant again
        if (nearGeometry) { newState = PROBE_STATE_NEWLY_VIGILANT; }
        break;
    }

    // ---------------------------------------------------------------------
    // Store result                                                          
    // ---------------------------------------------------------------------
    imageStore(ProbeStates, texelCoord, uvec4(newState, 0u, 0u, 0u));
} 
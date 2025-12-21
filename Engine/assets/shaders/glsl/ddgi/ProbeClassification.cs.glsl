#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require

#ifndef PROBE_OFFSETS_TEXTURE
#define PROBE_OFFSETS_TEXTURE
#endif


layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

// -----------------------------------------------------------------------------
//  Resource bindings                                                            
// -----------------------------------------------------------------------------
// 0,0  – Ray data written by the ProbeTrace pass                                
layout(set = 3, binding = 0) uniform sampler2DArray gTextureArrays[];

// 0,1  – Probe state image (R8_UINT)  (read-modify-write)
layout(set = 4, binding = 3, r8ui) uniform restrict uimage2DArray ProbeStates;

// -----------------------------------------------------------------------------
//  Shared DDGI helpers / structs                                                
// -----------------------------------------------------------------------------
#include "ProbeCommon.glsl"

layout(std140, set = 0, binding = 5) uniform ProbeInfo {
    ProbeVolume u_volume;
};

layout(push_constant) uniform PushConstants {
    uint rayDataIndex;
    uint probeOffsetHandle;
} pc;

// -----------------------------------------------------------------------------
//  Enumerated probe states (uint encodings)                                     
// -----------------------------------------------------------------------------
const uint PROBE_STATE_ACTIVE   = 0;
const uint PROBE_STATE_INACTIVE = 1;
const uint PROBE_STATE_INACTIVE_BACKFACE = 2;  // Debug: inactive due to backfaces
const uint PROBE_STATE_INACTIVE_NO_GEOMETRY = 3;  // Debug: inactive due to no geometry in cell


// Convenient alias
#define RAYS_PER_PROBE  (u_volume.probeNumRays)

void main() {

    uint probeIndex  = gl_GlobalInvocationID.x;
    uint totalProbes = u_volume.gridDimensions.x * u_volume.gridDimensions.y * u_volume.gridDimensions.z;
    if (probeIndex >= totalProbes) return;

    int numRays = min(u_volume.probeNumRays, int(u_volume.probeStaticRayCount));

    int rayIndex;
    int backfaceCount = 0;
    float hitDistances[32]; // static rays, should define a macro with value...

    for (rayIndex = 0; rayIndex < u_volume.probeStaticRayCount; rayIndex++) {
        ivec3 rayDataTexCoords = ivec3(DDGIGetRayDataTexelCoords(rayIndex, int(probeIndex), u_volume));

        hitDistances[rayIndex] = texelFetch(gTextureArrays[pc.rayDataIndex], rayDataTexCoords, 0).a;

        backfaceCount += (hitDistances[rayIndex] < 0.0) ? 1 : 0;
    }

    ivec3 outputCoords = ivec3(DDGIGetProbeTexelCoords(int(probeIndex), u_volume));

    if((float(backfaceCount) / float(u_volume.probeStaticRayCount)) > u_volume.probeFixedRayBackfaceThreshold)
    {
        imageStore(ProbeStates, outputCoords, uvec4(PROBE_STATE_INACTIVE_BACKFACE, 0u, 0u, 0u));
        return;
    }

    ivec3 probeCoords = DDGIGetProbeCoords(int(probeIndex), u_volume);
    vec3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, u_volume, gTextureArrays[pc.probeOffsetHandle]);


    for (rayIndex = 0; rayIndex < u_volume.probeStaticRayCount; rayIndex++)
    {
        if(hitDistances[rayIndex] < 0) continue;

        vec3 direction = DDGIGetProbeRayDirection(rayIndex, u_volume);

        vec3 xNormal = vec3(direction.x / max(abs(direction.x), 0.000001), 0.0, 0.0);
        vec3 yNormal = vec3(0.0, direction.y / max(abs(direction.y), 0.000001), 0.0);
        vec3 zNormal = vec3(0.0, 0.0, direction.z / max(abs(direction.z), 0.000001));

        // Get the relevant planes to intersect
        vec3 p0x = probeWorldPosition + (u_volume.spacing.x * xNormal);
        vec3 p0y = probeWorldPosition + (u_volume.spacing.y * yNormal);
        vec3 p0z = probeWorldPosition + (u_volume.spacing.z * zNormal);

        // Get the ray's intersection distance with each plane
        vec3 distances = vec3(
            dot((p0x - probeWorldPosition), xNormal) / max(dot(direction, xNormal), 0.000001),
            dot((p0y - probeWorldPosition), yNormal) / max(dot(direction, yNormal), 0.000001),
            dot((p0z - probeWorldPosition), zNormal) / max(dot(direction, zNormal), 0.000001)
        );

        // If the ray is parallel to the plane, it will never intersect
        // Set the distance to a very large number for those planes
        if (distances.x == 0.0) distances.x = 1e27;
        if (distances.y == 0.0) distances.y = 1e27;
        if (distances.z == 0.0) distances.z = 1e27;

        // Get the distance to the closest plane intersection
        float maxDistance = min(distances.x, min(distances.y, distances.z));

        // If the hit distance is less than the closest plane intersection, the probe should be active
        if(hitDistances[rayIndex] <= maxDistance)
        {
            imageStore(ProbeStates, outputCoords, uvec4(PROBE_STATE_ACTIVE, 0, 0, 0));
            return;
        }
    }

    imageStore(ProbeStates, outputCoords, uvec4(PROBE_STATE_INACTIVE_NO_GEOMETRY, 0, 0, 0));
}

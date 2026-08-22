#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 3, binding = 0) uniform sampler2DArray gTextureArrays[];
layout(set = 3, binding = 0) uniform usampler2DArray gUintTextureArrays[];

layout(push_constant) uniform PushConstants {
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint volumeSlot;
    uint probeIrradianceIndex;
    uint probeDistanceIndex;
    uint probeOffsetIndex;
    uint probeClassificationIndex;
    uint mode;
    float probeRadius;
} pc;

#include "ddgi/ProbeCommon.glsl"

layout(std140, set = 0, binding = 5) uniform ProbeInfo {
    ProbeVolume volume;
} u_probeInfo[];

ProbeVolume u_volume;

layout(location = 0) in vec3 vNormal;
layout(location = 1) flat in int vProbeIndex;

layout(location = 0) out vec4 outColor;

// matched by ProbeDebugMode in DDGIProbeDebugPass.h
const uint PDM_CLASSIFICATION = 0u;
const uint PDM_IRRADIANCE = 1u;
const uint PDM_DISTANCE = 2u;
const uint PDM_RELOCATION = 3u;

// matched by the probe states ProbeClassification.cs.glsl writes
const uint PROBE_STATE_ACTIVE = 0u;
const uint PROBE_STATE_INACTIVE = 1u;
const uint PROBE_STATE_INACTIVE_BACKFACE = 2u;
const uint PROBE_STATE_INACTIVE_NO_GEOMETRY = 3u;

const vec3 COL_ACTIVE = vec3(0.15, 0.85, 0.25);
const vec3 COL_INACTIVE = vec3(0.35, 0.35, 0.35);
const vec3 COL_BACKFACE = vec3(0.9, 0.15, 0.15);
const vec3 COL_NO_GEOMETRY = vec3(0.95, 0.8, 0.1);
const vec3 COL_UNKNOWN = vec3(1.0, 0.0, 1.0);
const vec3 COL_RELOCATED = vec3(0.2, 0.5, 1.0);

vec3 stateColor(uint state)
{
    if (state == PROBE_STATE_ACTIVE) {
        return COL_ACTIVE;
    }
    if (state == PROBE_STATE_INACTIVE) {
        return COL_INACTIVE;
    }
    if (state == PROBE_STATE_INACTIVE_BACKFACE) {
        return COL_BACKFACE;
    }
    if (state == PROBE_STATE_INACTIVE_NO_GEOMETRY) {
        return COL_NO_GEOMETRY;
    }
    return COL_UNKNOWN;
}

void main() {
    u_volume = u_probeInfo[pc.volumeSlot].volume;

    vec3 normal = normalize(vNormal);
    vec2 octantCoords = DDGIGetOctahedralCoordinates(normal);

    vec3 color = COL_UNKNOWN;

    if (pc.mode == PDM_CLASSIFICATION) {
        uvec3 texelCoords = DDGIGetProbeTexelCoords(vProbeIndex, u_volume);
        uint state = texelFetch(gUintTextureArrays[pc.probeClassificationIndex], ivec3(texelCoords), 0).r;
        color = stateColor(state);

    } else if (pc.mode == PDM_IRRADIANCE) {
        vec3 uv = DDGIGetProbeUV(vProbeIndex, octantCoords, u_volume.probeNumIrradianceInteriorTexels, u_volume);
        vec3 irradiance = textureLod(gTextureArrays[pc.probeIrradianceIndex], uv, 0.0).rgb;
        irradiance = pow(irradiance, vec3(u_volume.probeIrradianceEncodingGamma * 0.5));
        color = irradiance * irradiance;

    } else if (pc.mode == PDM_DISTANCE) {
        vec3 uv = DDGIGetProbeUV(vProbeIndex, octantCoords, u_volume.probeNumDistanceInteriorTexels, u_volume);
        float distance = 2.0 * textureLod(gTextureArrays[pc.probeDistanceIndex], uv, 0.0).r;
        color = vec3(distance / max(length(u_volume.spacing) * 1.5, 1e-6));

    } else if (pc.mode == PDM_RELOCATION) {
        uvec3 texelCoords = DDGIGetProbeTexelCoords(vProbeIndex, u_volume);
        vec3 storedOffset = texelFetch(gTextureArrays[pc.probeOffsetIndex], ivec3(texelCoords), 0).xyz;
        vec3 worldOffset = DDGILoadProbeDataOffset(storedOffset, u_volume);
        // the relocation shader clamps to 0.45 of the spacing, so that is full scale here
        color = COL_RELOCATED * (length(worldOffset / u_volume.spacing) / 0.45);
    }

    outColor = vec4(color, 1.0);
}

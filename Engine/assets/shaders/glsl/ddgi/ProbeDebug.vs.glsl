#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

#include "common/CameraCommon.glsl"

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

layout(location = 0) out vec3 vNormal;
layout(location = 1) flat out int vProbeIndex;

// matched by PROBE_SPHERE_* in DDGIProbeDebugPass.cpp
const uint PROBE_SPHERE_RINGS = 8u;
const uint PROBE_SPHERE_SECTORS = 16u;

// the two triangles of one ring/sector quad, each corner naming a ring and a sector step
const ivec2 QUAD_CORNERS[6] = ivec2[6](ivec2(0, 0), ivec2(0, 1), ivec2(1, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));

vec3 sphereDirection(uint vertexIndex)
{
    uint quad = vertexIndex / 6u;
    ivec2 corner = QUAD_CORNERS[vertexIndex % 6u];

    uint ring = quad / PROBE_SPHERE_SECTORS + uint(corner.x);
    uint sector = quad % PROBE_SPHERE_SECTORS + uint(corner.y);

    float phi = float(ring) / float(PROBE_SPHERE_RINGS) * 3.14159265359;
    float theta = float(sector) / float(PROBE_SPHERE_SECTORS) * 6.28318530718;

    return vec3(sin(phi) * cos(theta), cos(phi), sin(phi) * sin(theta));
}

void main() {
    u_volume = u_probeInfo[pc.volumeSlot].volume;

    vProbeIndex = gl_InstanceIndex;

    ivec3 probeCoords = DDGIGetProbeCoords(gl_InstanceIndex, u_volume);
    vec3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, u_volume, gTextureArrays[pc.probeOffsetIndex]);

    vec3 direction = sphereDirection(uint(gl_VertexIndex));
    vNormal = direction;

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];

    gl_Position = cam.proj * cam.view * vec4(probeWorldPosition + direction * pc.probeRadius, 1.0);
}

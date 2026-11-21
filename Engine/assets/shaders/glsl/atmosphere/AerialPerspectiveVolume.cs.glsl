#version 460

#extension GL_EXT_nonuniform_qualifier : require

// Light scattered towards the camera and the transmittance back to it, in a volume fit to the view frustum.
// Depth slices are spaced quadratically, finer close to the camera.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, rgba16f) uniform restrict writeonly image3D outAerialPerspective;

#include "atmosphere/AtmosphereCommon.glsl"
#include "common/CameraCommon.glsl"

layout(push_constant) uniform PushConstants {
    vec4 cameraPosition; // xyz world metres
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
} pc;

void main() {
    ivec3 voxel = ivec3(gl_GlobalInvocationID.xyz);
    int size = int(AERIAL_PERSPECTIVE_SIZE);
    if (voxel.x >= size || voxel.y >= size || voxel.z >= size) {
        return;
    }

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];

    vec2 uv = (vec2(voxel.xy) + 0.5) / float(size);
    vec4 farPoint = cam.invViewProj * vec4(uv * 2.0 - 1.0, 0.5, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - pc.cameraPosition.xyz);

    float slice = (float(voxel.z) + 0.5) / float(size);
    slice = slice * slice * float(size);
    float voxelDistance = slice * AERIAL_PERSPECTIVE_KM_PER_SLICE;

    vec3 origin = atmosphereLocalFromWorld(pc.cameraPosition.xyz);

    // A voxel below the ground is pulled up onto it, a large voxel would otherwise integrate through the planet
    vec3 voxelPos = origin + dir * voxelDistance;
    if (planetAltitude(voxelPos, atmosphereGroundRadius()) <= PLANET_RADIUS_OFFSET_KM) {
        vec3 up = planetUp(voxelPos, atmosphereGroundRadius());
        float lift = PLANET_RADIUS_OFFSET_KM + 0.001 - planetAltitude(voxelPos, atmosphereGroundRadius());
        voxelPos += up * lift;
        dir = normalize(voxelPos - origin);
        voxelDistance = length(voxelPos - origin);
    }

    float steps = max(1.0, float(voxel.z + 1) * 2.0);
    uint flags = ATMOSPHERE_MARCH_PHASE | ATMOSPHERE_MARCH_MULTIPLE_SCATTERING;
    AtmosphereRaymarchResult march = atmosphereRaymarch(origin, dir, u_atmosphere.sunDirection.xyz, steps, voxelDistance, flags);

    float meanTransmittance = dot(march.transmittance, vec3(1.0 / 3.0));
    imageStore(outAerialPerspective, voxel, vec4(march.luminance * u_atmosphere.sunIlluminance.rgb, 1.0 - meanTransmittance));
}

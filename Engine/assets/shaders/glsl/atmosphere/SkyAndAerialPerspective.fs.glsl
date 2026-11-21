#version 460

#extension GL_EXT_nonuniform_qualifier : require

// Draws the sky behind the scene and the atmosphere between the camera and everything in front of it.
// Blended as color = source + destination * (1 - source alpha): the sky replaces the background, aerial
// perspective adds in-scattered light and dims the lit surface by the transmittance.

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];
layout(set = 3, binding = 0) uniform sampler3D gVolumes[];

#include "atmosphere/AtmosphereCommon.glsl"
#include "common/CameraCommon.glsl"

layout(push_constant) uniform PushConstants {
    vec4 cameraPosition; // xyz world metres, w = altitude in km
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint depthTexture;
    uint skyViewLut;
    uint aerialPerspectiveVolume;
    float skyIntensity;
} pc;

// Past this the half float scene colour overflows
const float MAX_SUN_LUMINANCE = 60000.0;

vec3 sunDiskLuminance(vec3 _dir, float _altitude) {
    vec3 sunDir = u_atmosphere.sunDirection.xyz;
    float cosDiskRadius = u_atmosphere.sunIlluminance.w;
    float cosToSun = dot(_dir, sunDir);
    if (cosToSun < cosDiskRadius || _dir.y < planetHorizonCosZenith(_altitude, atmosphereGroundRadius())) {
        return vec3(0.0);
    }

    float solidAngle = 2.0 * MEDIA_PI * (1.0 - cosDiskRadius);
    vec3 transmittance = atmosphereSampleTransmittance(_altitude, sunDir.y);
    vec3 luminance = u_atmosphere.sunIlluminance.rgb * transmittance / solidAngle;

    // Softens the rim over its outer tenth, the disk is only a few pixels wide
    float edge = smoothstep(cosDiskRadius, mix(cosDiskRadius, 1.0, 0.1), cosToSun);
    return min(luminance, vec3(MAX_SUN_LUMINANCE)) * edge;
}

void main() {
    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];
    float depth = texture(gTextures[nonuniformEXT(pc.depthTexture)], fragTexCoord).r;
    vec2 ndc = fragTexCoord * 2.0 - 1.0;

    if (depth >= 1.0) {
        vec4 farPoint = cam.invViewProj * vec4(ndc, 1.0, 1.0);
        vec3 dir = normalize(farPoint.xyz / farPoint.w - pc.cameraPosition.xyz);

        float altitude = pc.cameraPosition.w;
        float cosLightView = atmosphereCosLightView(dir, u_atmosphere.sunDirection.xyz);
        vec2 uv = atmosphereSkyViewUv(altitude, dir.y, cosLightView);
        vec3 sky = textureLod(gTextures[nonuniformEXT(pc.skyViewLut)], uv, 0.0).rgb * pc.skyIntensity;

        outColor = vec4(sky + sunDiskLuminance(dir, altitude), 1.0);
        return;
    }

    vec4 worldH = cam.invViewProj * vec4(ndc, depth, 1.0);
    vec3 worldPos = worldH.xyz / worldH.w;
    float distanceKm = length(worldPos - pc.cameraPosition.xyz) * 0.001;

    // Slices are spaced quadratically, and the first half slice fades to nothing at the camera
    float slice = distanceKm / AERIAL_PERSPECTIVE_KM_PER_SLICE;
    float weight = 1.0;
    if (slice < 0.5) {
        weight = clamp(slice * 2.0, 0.0, 1.0);
        slice = 0.5;
    }
    float w = sqrt(slice / float(AERIAL_PERSPECTIVE_SIZE));

    vec4 aerial = textureLod(gVolumes[nonuniformEXT(pc.aerialPerspectiveVolume)], vec3(fragTexCoord, w), 0.0) * weight;
    outColor = vec4(aerial.rgb * pc.skyIntensity, aerial.a);
}

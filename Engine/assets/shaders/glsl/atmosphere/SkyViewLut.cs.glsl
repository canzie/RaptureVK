#version 460

#extension GL_EXT_nonuniform_qualifier : require

// The distant sky around a viewer as a latitude/longitude map, the ground below the horizon included

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, rgba16f) uniform restrict writeonly image2D outSkyView;

#include "atmosphere/AtmosphereCommon.glsl"

layout(push_constant) uniform PushConstants {
    float viewerAltitude; // km
} pc;

const float MAX_STEP_COUNT = 32.0;

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (texel.x >= int(SKY_VIEW_LUT_WIDTH) || texel.y >= int(SKY_VIEW_LUT_HEIGHT)) {
        return;
    }

    vec2 uv = (vec2(texel) + 0.5) / vec2(SKY_VIEW_LUT_WIDTH, SKY_VIEW_LUT_HEIGHT);
    float cosZenith;
    float cosLightView;
    atmosphereSkyViewParams(uv, pc.viewerAltitude, cosZenith, cosLightView);

    // The sun is turned into the xy plane, the table only stores the azimuth relative to it
    float cosSunZenith = u_atmosphere.sunDirection.y;
    vec3 sunDir = vec3(sqrt(max(1.0 - cosSunZenith * cosSunZenith, 0.0)), cosSunZenith, 0.0);

    float sinZenith = sqrt(max(1.0 - cosZenith * cosZenith, 0.0));
    vec3 dir = vec3(sinZenith * cosLightView, cosZenith, sinZenith * sqrt(max(1.0 - cosLightView * cosLightView, 0.0)));

    vec3 origin = vec3(0.0, pc.viewerAltitude, 0.0);
    uint flags = ATMOSPHERE_MARCH_PHASE | ATMOSPHERE_MARCH_MULTIPLE_SCATTERING | ATMOSPHERE_MARCH_GROUND |
                 ATMOSPHERE_MARCH_VARIABLE_STEPS;
    AtmosphereRaymarchResult march = atmosphereRaymarch(origin, dir, sunDir, MAX_STEP_COUNT, 1e9, flags);

    imageStore(outSkyView, texel, vec4(march.luminance * u_atmosphere.sunIlluminance.rgb, 1.0));
}

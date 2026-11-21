#version 460

#extension GL_EXT_nonuniform_qualifier : require

// Transmittance from a point to the top of the atmosphere, by altitude and view zenith angle

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, rgba16f) uniform restrict writeonly image2D outTransmittance;

#include "atmosphere/AtmosphereCommon.glsl"

const float STEP_COUNT = 40.0;

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (texel.x >= int(TRANSMITTANCE_LUT_WIDTH) || texel.y >= int(TRANSMITTANCE_LUT_HEIGHT)) {
        return;
    }

    vec2 uv = (vec2(texel) + 0.5) / vec2(TRANSMITTANCE_LUT_WIDTH, TRANSMITTANCE_LUT_HEIGHT);
    float altitude;
    float cosZenith;
    atmosphereTransmittanceParams(uv, altitude, cosZenith);

    vec3 origin = vec3(0.0, altitude, 0.0);
    vec3 dir = vec3(sqrt(max(1.0 - cosZenith * cosZenith, 0.0)), cosZenith, 0.0);

    // Only the optical depth is needed, so the march is done here rather than through atmosphereRaymarch, which
    // would read this very table
    float tEnd = planetIntersectShellNearest(origin, dir, atmosphereGroundRadius(), atmosphereThickness());
    vec3 opticalDepth = vec3(0.0);
    if (tEnd > 0.0) {
        float dt = tEnd / STEP_COUNT;
        for (float s = 0.0; s < STEP_COUNT; s += 1.0) {
            vec3 samplePos = origin + (s + 0.5) * dt * dir;
            AtmosphereMedium medium = atmosphereSampleMedium(max(planetAltitude(samplePos, atmosphereGroundRadius()), 0.0));
            opticalDepth += medium.extinction * dt;
        }
    }

    imageStore(outTransmittance, texel, vec4(exp(-opticalDepth), 1.0));
}

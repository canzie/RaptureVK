#version 460

#extension GL_EXT_nonuniform_qualifier : require

// Psi_ms (Equation 10): the light reaching a point after two or more scattering events, per unit of sun
// illuminance, by sun zenith angle and altitude. One work group per texel, one thread per sphere direction.

layout(local_size_x = 1, local_size_y = 1, local_size_z = 64) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, rgba16f) uniform restrict writeonly image2D outMultipleScattering;

#include "atmosphere/AtmosphereCommon.glsl"

const uint SQRT_DIRECTION_COUNT = 8;
const uint DIRECTION_COUNT = SQRT_DIRECTION_COUNT * SQRT_DIRECTION_COUNT;
const float STEP_COUNT = 20.0;

shared vec3 s_secondOrder[DIRECTION_COUNT];
shared vec3 s_transfer[DIRECTION_COUNT];

void main() {
    ivec2 texel = ivec2(gl_WorkGroupID.xy);
    uint direction = gl_LocalInvocationID.z;

    float size = float(MULTIPLE_SCATTERING_LUT_SIZE);
    vec2 uv = (vec2(texel) + 0.5) / size;
    uv = vec2(lookupTableSubUvToUnit(uv.x, size), lookupTableSubUvToUnit(uv.y, size));

    float cosSunZenith = uv.x * 2.0 - 1.0;
    vec3 sunDir = vec3(0.0, cosSunZenith, sqrt(clamp(1.0 - cosSunZenith * cosSunZenith, 0.0, 1.0)));
    float altitude = clamp(uv.y, 0.0, 1.0) * (atmosphereThickness() - PLANET_RADIUS_OFFSET_KM) + PLANET_RADIUS_OFFSET_KM;
    vec3 origin = vec3(0.0, altitude, 0.0);

    // A uniform grid over the sphere, uniform in azimuth and in the cosine of the polar angle
    float i = 0.5 + float(direction / SQRT_DIRECTION_COUNT);
    float j = 0.5 + float(direction % SQRT_DIRECTION_COUNT);
    float azimuth = 2.0 * MEDIA_PI * i / float(SQRT_DIRECTION_COUNT);
    float polar = acos(1.0 - 2.0 * j / float(SQRT_DIRECTION_COUNT));
    vec3 dir = vec3(cos(azimuth) * sin(polar), cos(polar), sin(azimuth) * sin(polar));

    uint flags = ATMOSPHERE_MARCH_GROUND;
    AtmosphereRaymarchResult march = atmosphereRaymarch(origin, dir, sunDir, STEP_COUNT, 1e9, flags);

    // Each direction stands for an equal share of the sphere's 4 pi steradians
    float solidAngle = 4.0 * MEDIA_PI / float(DIRECTION_COUNT);
    s_secondOrder[direction] = march.luminance * solidAngle;
    s_transfer[direction] = march.multipleScatteringTransfer * solidAngle;

    barrier();

    for (uint stride = DIRECTION_COUNT / 2; stride > 0; stride /= 2) {
        if (direction < stride) {
            s_secondOrder[direction] += s_secondOrder[direction + stride];
            s_transfer[direction] += s_transfer[direction + stride];
        }
        barrier();
    }

    if (direction != 0) {
        return;
    }

    // Equations 5 and 7, the isotropic phase function applied to the sphere integrals
    vec3 secondOrder = s_secondOrder[0] * mediaUniformPhase();
    vec3 transfer = s_transfer[0] * mediaUniformPhase();

    // Equation 9, every further order is one more f_ms, summed as a geometric series
    vec3 allOrders = 1.0 / max(vec3(1.0) - transfer, vec3(1e-4));

    imageStore(outMultipleScattering, texel, vec4(secondOrder * allOrders, 1.0));
}

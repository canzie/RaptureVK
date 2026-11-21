#version 460

#extension GL_EXT_nonuniform_qualifier : require

// The sky seen from the world origin in every direction, the sunlit ground below the horizon included and
// the sun disk left out

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, rgba16f) uniform restrict writeonly image2DArray outCubemap;

#include "atmosphere/AtmosphereCommon.glsl"

const float MAX_STEP_COUNT = 32.0;

// Face order matches Vulkan cube layers: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
vec3 cubeFaceDirection(int _face, vec2 _uv) {
    vec3 dir;
    if (_face == 0) {
        dir = vec3(1.0, -_uv.y, -_uv.x);
    } else if (_face == 1) {
        dir = vec3(-1.0, -_uv.y, _uv.x);
    } else if (_face == 2) {
        dir = vec3(_uv.x, 1.0, _uv.y);
    } else if (_face == 3) {
        dir = vec3(_uv.x, -1.0, -_uv.y);
    } else if (_face == 4) {
        dir = vec3(_uv.x, -_uv.y, 1.0);
    } else {
        dir = vec3(-_uv.x, -_uv.y, -1.0);
    }
    return normalize(dir);
}

void main() {
    ivec2 size = imageSize(outCubemap).xy;
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (texel.x >= size.x || texel.y >= size.y) {
        return;
    }

    int face = int(gl_GlobalInvocationID.z);
    vec2 uv = (vec2(texel) + 0.5) / vec2(size) * 2.0 - 1.0;
    vec3 dir = cubeFaceDirection(face, uv);

    vec3 origin = atmosphereLocalFromWorld(vec3(0.0));
    uint flags = ATMOSPHERE_MARCH_PHASE | ATMOSPHERE_MARCH_MULTIPLE_SCATTERING | ATMOSPHERE_MARCH_GROUND |
                 ATMOSPHERE_MARCH_VARIABLE_STEPS;
    AtmosphereRaymarchResult march = atmosphereRaymarch(origin, dir, u_atmosphere.sunDirection.xyz, MAX_STEP_COUNT, 1e9, flags);

    imageStore(outCubemap, ivec3(texel, face), vec4(march.luminance * u_atmosphere.sunIlluminance.rgb, 1.0));
}

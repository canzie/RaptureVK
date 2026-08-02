#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 fragTexCoord;

#include "common/Tonemapping.glsl"

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(push_constant) uniform PushConstants {
    uint sceneColorHandle;
    float exposureStops;
} pc;

void main() {
    vec3 color = texture(gTextures[nonuniformEXT(pc.sceneColorHandle)], fragTexCoord).rgb;

    color *= exposure(pc.exposureStops);
    color = GT7ToneMapping(color);

// Defined when the target is not an _SRGB format, so the hardware does not encode on write
#ifdef COMPOSITE_APPLY_SRGB_ENCODE
    color = LinearToSRGB(color);
#endif

    outColor = vec4(color, 1.0);
}

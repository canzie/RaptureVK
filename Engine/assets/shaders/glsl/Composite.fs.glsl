#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 fragTexCoord;

#include "common/RenderFlags.glsl"
#include "common/Tonemapping.glsl"

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(push_constant) uniform PushConstants {
    uint sceneColorHandle;
    float exposureStops;
    uint renderFlags;
    uint debugTextureHandle;
} pc;

void main() {
    // A view that replaces the scene has to do it here rather than in the lighting shader, because
    // the lighting shader writes the buffer a later frame reads back as its reflection source.
    // Drawing a visualisation into it makes the reflections reflect the visualisation, and that
    // compounds every frame. Nothing reads back what this pass writes.
    bool showHit = (pc.renderFlags & RENDER_SHOW_SSSR_HIT) != 0u;
    bool showAlpha = (pc.renderFlags & (RENDER_SHOW_SSSR_CONFIDENCE | RENDER_SHOW_AMBIENT_OCCLUSION)) != 0u;
    bool showDirection = (pc.renderFlags & RENDER_SHOW_BENT_NORMALS) != 0u;
    bool showRadiance = (pc.renderFlags & (RENDER_SHOW_SSSR_RESOLVED | RENDER_SHOW_SSSR_ACCUMULATED)) != 0u;

    vec3 color;

    if (showHit) {
        // Where the ray landed, as a uv. Red rises to the right, green downwards, black is a miss.
        // The sampling density in w is what marks the record usable.
        vec4 hit = texture(gTextures[nonuniformEXT(pc.debugTextureHandle)], fragTexCoord);
        color = vec3(hit.xy * (hit.w > 0.0 ? 1.0 : 0.0), 0.0);
    } else if (showAlpha) {
        // Negative marks a record with no rays behind it, which reads the same as empty here
        color = vec3(max(texture(gTextures[nonuniformEXT(pc.debugTextureHandle)], fragTexCoord).a, 0.0));
    } else if (showDirection) {
        // A world direction, shown the same way the normal view shows one
        color = texture(gTextures[nonuniformEXT(pc.debugTextureHandle)], fragTexCoord).rgb * 0.5 + 0.5;
    } else {
        uint source = showRadiance ? pc.debugTextureHandle : pc.sceneColorHandle;
        color = texture(gTextures[nonuniformEXT(source)], fragTexCoord).rgb;

        // Radiance gets the same curve the scene does, since it is light and would otherwise clip.
        // The views above are measurements in a known range and are shown as they are.
        color *= exposure(pc.exposureStops);
        color = ACESFilm(color);
    }

// Defined when the target is not an _SRGB format, so the hardware does not encode on write
#ifdef COMPOSITE_APPLY_SRGB_ENCODE
    color = LinearToSRGB(color);
#endif

    outColor = vec4(color, 1.0);
}

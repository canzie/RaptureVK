#version 450

#include "common/Tonemapping.glsl"

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 4, binding = 0) uniform usampler2D u_entityId;

layout(push_constant) uniform PushConstants {
    vec4 outlineColor;
    uint selectedEntityId;
    int thickness;
} pc;

// Ids are stored biased by one, so a texel reading zero is one nothing was drawn into
bool isSelectedTexel(ivec2 _coord, ivec2 _bounds) {
    uint biased = texelFetch(u_entityId, clamp(_coord, ivec2(0), _bounds), 0).r;
    return biased != 0u && biased - 1u == pc.selectedEntityId;
}

void main() {
    ivec2 size = textureSize(u_entityId, 0);
    ivec2 bounds = size - ivec2(1);
    ivec2 center = ivec2(fragTexCoord * vec2(size));

    // The band sits outside the silhouette, so anything the selection covers is left alone
    if (isSelectedTexel(center, bounds)) {
        discard;
    }

    for (int y = -pc.thickness; y <= pc.thickness; ++y) {
        for (int x = -pc.thickness; x <= pc.thickness; ++x) {
            if (!isSelectedTexel(center + ivec2(x, y), bounds)) {
                continue;
            }

            // The scene colour is linear, the border colour is authored for display
            outColor = vec4(SRGBToLinear(pc.outlineColor.rgb), pc.outlineColor.a);
            return;
        }
    }

    discard;
}

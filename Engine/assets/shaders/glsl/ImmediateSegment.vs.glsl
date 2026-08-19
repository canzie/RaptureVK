#version 450

#extension GL_EXT_nonuniform_qualifier : require

#include "common/CameraCommon.glsl"

struct LineSegment {
    vec3 start;
    float thickness;
    vec3 end;
    uint color;
};

layout(std430, set = 4, binding = 0) readonly buffer ShapeBuffer {
    LineSegment segments[];
} u_shapes;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint shapeOffset;
} pc;

layout(location = 0) out vec4 vColor;

const float MIN_CLIP_W = 1e-4;

// Two triangles over the four corners of the quad, each corner naming an endpoint and a side
const int QUAD_CORNERS[6] = int[6](0, 1, 2, 2, 1, 3);

void main() {
    LineSegment segment = u_shapes.segments[pc.shapeOffset + gl_InstanceIndex];

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];
    mat4 viewProj = cam.proj * cam.view;

    vec4 clipStart = viewProj * vec4(segment.start, 1.0);
    vec4 clipEnd = viewProj * vec4(segment.end, 1.0);

    if (clipStart.w < MIN_CLIP_W && clipEnd.w < MIN_CLIP_W) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        vColor = vec4(0.0);
        return;
    }

    // An endpoint behind the eye has to be pulled onto the near plane before the perspective divide,
    // or its projection flips and the line is drawn across the screen
    if (clipStart.w < MIN_CLIP_W) {
        clipStart = mix(clipStart, clipEnd, (MIN_CLIP_W - clipStart.w) / (clipEnd.w - clipStart.w));
    } else if (clipEnd.w < MIN_CLIP_W) {
        clipEnd = mix(clipEnd, clipStart, (MIN_CLIP_W - clipEnd.w) / (clipStart.w - clipEnd.w));
    }

    vec2 ndcStart = clipStart.xy / clipStart.w;
    vec2 ndcEnd = clipEnd.xy / clipEnd.w;

    vec2 pixelDelta = (ndcEnd - ndcStart) * pc.viewportSize * 0.5;
    float pixelLength = length(pixelDelta);
    vec2 direction = pixelLength > 0.0 ? pixelDelta / pixelLength : vec2(1.0, 0.0);
    vec2 normal = vec2(-direction.y, direction.x);

    int corner = QUAD_CORNERS[gl_VertexIndex];
    bool atEnd = (corner & 2) != 0;
    float side = (corner & 1) != 0 ? 1.0 : -1.0;

    vec4 clip = atEnd ? clipEnd : clipStart;
    vec2 ndc = atEnd ? ndcEnd : ndcStart;

    // Thickness is authored in pixels, so the offset is built in pixels and taken back into ndc
    vec2 ndcOffset = normal * side * segment.thickness / pc.viewportSize;

    gl_Position = vec4((ndc + ndcOffset) * clip.w, clip.z, clip.w);
    vColor = unpackUnorm4x8(segment.color);
}

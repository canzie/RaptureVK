#version 450

#extension GL_EXT_nonuniform_qualifier : require

#include "common/CameraCommon.glsl"

struct ShapeVertex {
    vec3 position;
    uint color;
};

layout(std430, set = 4, binding = 0) readonly buffer ShapeBuffer {
    ShapeVertex vertices[];
} u_shapes;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint shapeOffset;
} pc;

layout(location = 0) out vec4 vColor;

#ifdef USE_SHADED_MODE
layout(location = 1) out vec3 vViewPosition;
#endif // USE_SHADED_MODE

void main() {
    ShapeVertex shapeVertex = u_shapes.vertices[pc.shapeOffset + gl_VertexIndex];

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];

    vec4 viewPosition = cam.view * vec4(shapeVertex.position, 1.0);

    gl_Position = cam.proj * viewPosition;
    vColor = unpackUnorm4x8(shapeVertex.color);

#ifdef USE_SHADED_MODE
    vViewPosition = viewPosition.xyz;
#endif // USE_SHADED_MODE
}

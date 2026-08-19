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

void main() {
    ShapeVertex shapeVertex = u_shapes.vertices[pc.shapeOffset + gl_VertexIndex];

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];

    gl_Position = cam.proj * cam.view * vec4(shapeVertex.position, 1.0);
    vColor = unpackUnorm4x8(shapeVertex.color);
}

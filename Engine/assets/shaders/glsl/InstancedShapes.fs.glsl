#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec4 inColor;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 globalTransform;
    vec4 color;
    uint cameraUBOIndex;
    uint instanceDataSSBOIndex;
} pc;

void main() {
    outColor = inColor;
} 
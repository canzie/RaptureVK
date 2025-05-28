#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aTangent;

layout(location = 0) out vec3 fragColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

// Push constant for per-object data
layout(push_constant) uniform PushConstants {
    mat4 model;
} pushConstants;

void main() {
    gl_Position = ubo.proj * ubo.view * pushConstants.model * vec4(aPosition, 1.0);
    fragColor = vec3(1.0, 0.0, 0.0);
}
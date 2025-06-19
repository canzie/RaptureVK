#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;


layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vPosition;


layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

// Push constant for per-object data
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    float borderWidth;
} pushConstants;

void main() {
    mat4 modelView = ubo.view * pushConstants.model;
    vec4 posView = modelView * vec4(aPosition, 1.0);

    // Correctly transform normal to view space to handle non-uniform scaling
    mat3 normalMatrix = transpose(inverse(mat3(modelView)));
    vec3 normalView = normalize(normalMatrix * aNormal);

    // Extrude vertex in view space for consistent border width
    posView.xyz += normalView * pushConstants.borderWidth;

    gl_Position = ubo.proj * posView;
    
    // These are not used by the current fragment shader but are kept for potential future use.
    vNormal = mat3(pushConstants.model) * aNormal;
    vPosition = (pushConstants.model * vec4(aPosition, 1.0)).xyz;
}
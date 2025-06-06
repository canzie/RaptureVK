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
    // Transform normal to world space
    vNormal = mat3(pushConstants.model) * aNormal;
    
    // Calculate position in world space
    vec4 worldPos = pushConstants.model * vec4(aPosition, 1.0);
    vPosition = worldPos.xyz;
    
    // Expand vertices along normal direction for border effect
    vec3 expandedPos = aPosition + aNormal * pushConstants.borderWidth;
    
    // Transform to clip space
    gl_Position = ubo.proj * ubo.view * pushConstants.model * vec4(expandedPos, 1.0);
}
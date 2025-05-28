#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

// Push constant for per-object data
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec3 camPos;
} pushConstants;

void main() {
    // Transform position
    gl_Position = ubo.proj * ubo.view * pushConstants.model * vec4(aPosition, 1.0);
    
    // For now, let's use a simpler approach to avoid normal matrix issues
    // This works well when there's no non-uniform scaling
    mat3 modelMatrix = mat3(pushConstants.model);
    

    // Transform and pass vectors to fragment shader
    vec3 N = normalize(modelMatrix * aNormal);
    vec3 T = normalize(modelMatrix * aTangent.xyz);
    
    fragNormal = N;
    fragTangent = T;
    
    // Calculate bitangent using the handedness sign from aTangent.w
    fragBitangent = normalize(cross(N, T) * aTangent.w);
    
    // Pass through other attributes
    fragTexCoord = aTexCoord;
    fragPos = vec3(pushConstants.model * vec4(aPosition, 1.0));
}
#version 450

layout(location = 0) in vec3 aPosition;

// Uniforms

layout(set = 0, binding = 0) uniform ShadowMatricesUBO
{
	mat4 lightSpaceMatrix;
} shadowMatrices;



// Push constant for per-object data
layout(push_constant) uniform PushConstants {
    mat4 model;
} pushConstants;

void main() {
    
    gl_Position = shadowMatrices.lightSpaceMatrix * pushConstants.model * vec4(aPosition, 1.0);
}
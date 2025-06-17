#version 450
#extension GL_EXT_multiview : require

// Uniform buffer containing light view-projection matrices for all cascades
layout(set = 0, binding = 0) uniform CascadeMatrices {
    mat4 lightViewProjection[4];  // Support up to 4 cascades
} cascades;

// Push constants for the model matrix
layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

// Vertex attributes
layout(location = 0) in vec3 inPosition;

void main() {
    // gl_ViewIndex is automatically provided by the multiview extension
    // It corresponds to which cascade/view we're rendering to (0, 1, 2, or 3)
    
    // Transform vertex position to world space, then to light space for the current cascade
    vec4 worldPosition = pc.model * vec4(inPosition, 1.0);
    gl_Position = cascades.lightViewProjection[gl_ViewIndex] * worldPosition;
} 
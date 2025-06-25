#version 450


layout(location = 0) in vec3 aPosition;

// Uniforms



// Push constant for per-object data
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 shadowMatrix;
} pc;

void main() {
    
    gl_Position = pc.shadowMatrix * pc.model * vec4(aPosition, 1.0);
}
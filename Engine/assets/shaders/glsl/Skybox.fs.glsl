#version 460 core

// Input from vertex shader
layout(location = 0) in vec3 localPosition;

// Output
layout(location = 0) out vec4 FragColor;

// Skybox cubemap texture
layout(set = 0, binding = 1) uniform samplerCube u_skyboxCubemap;

void main() {
    // Sample the cubemap using the interpolated local position as direction
    vec3 skyboxColor = texture(u_skyboxCubemap, localPosition).rgb;
    
    // Output the sampled color with full alpha
    FragColor = vec4(skyboxColor, 1.0);
} 
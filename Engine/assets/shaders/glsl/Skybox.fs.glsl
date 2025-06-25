#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

// Input from vertex shader
layout(location = 0) in vec3 localPosition;

// Output
layout(location = 0) out vec4 FragColor;

// Skybox cubemap texture
layout(set = 3, binding = 0) uniform samplerCube u_gTextures[];

// Push constant - matching C++ SkyboxPass::PushConstants
layout(push_constant) uniform PushConstants {
    uint frameIndex;
    uint skyboxTextureIndex;
} pc;

void main() {
    // Sample the cubemap using the interpolated local position as direction
    vec3 skyboxColor = texture(u_gTextures[pc.skyboxTextureIndex], localPosition).rgb;
    
    // Output the sampled color with full alpha
    FragColor = vec4(skyboxColor, 1.0);
} 
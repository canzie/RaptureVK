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
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint skyboxTextureIndex;
    float skyIntensity;
} pc;

void main() {
    // Sample the cubemap using the interpolated local position as direction
    vec3 skyboxColor = texture(u_gTextures[pc.skyboxTextureIndex], localPosition).rgb;

    // The same scale the DDGI miss ray applies, so the sky lights the scene as brightly as it looks
    FragColor = vec4(skyboxColor * pc.skyIntensity, 1.0);
} 
#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

// No vertex input - we'll generate a fullscreen triangle in the vertex shader
layout(location = 0) in vec3 aPosition;

// Output to fragment shader
layout(location = 0) out vec3 localPosition;

struct CameraGPUData {
    mat4 view;
    mat4 proj;
};

layout(set = 0, binding = 0) readonly buffer CameraDataSSBO {
    CameraGPUData cameras[];
} u_cameraSSBO[];

layout(push_constant) uniform PushConstants {
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint skyboxTextureIndex;
} pc;

void main() {
    localPosition = aPosition;

    mat4 view = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex].view;
    mat4 proj = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex].proj;

    // Remove translation from view matrix for skybox
    mat4 viewNoTranslation = mat4(mat3(view));

    // Transform to clip space
    vec4 clipPos = proj * viewNoTranslation * vec4(aPosition, 1.0);
    
    // Set depth to maximum (far plane) by setting z = w
    // This ensures the skybox is always rendered behind everything else
    gl_Position = clipPos.xyww;
} 
#version 460 core

// No vertex input - we'll generate a fullscreen triangle in the vertex shader
layout(location = 0) in vec3 aPosition;

// Output to fragment shader
layout(location = 0) out vec3 localPosition;

// Camera matrices
layout(std140, set = 0, binding = 0) uniform CameraUniformBufferObject {
    mat4 view;
    mat4 proj;
} camera;

void main() {
    // Use the input position directly (assuming it's a unit cube)
    localPosition = aPosition;
    
    // Remove translation from view matrix for skybox
    mat4 viewNoTranslation = mat4(mat3(camera.view));
    
    // Transform to clip space
    vec4 clipPos = camera.proj * viewNoTranslation * vec4(aPosition, 1.0);
    
    // Set depth to maximum (far plane) by setting z = w
    // This ensures the skybox is always rendered behind everything else
    gl_Position = clipPos.xyww;
} 
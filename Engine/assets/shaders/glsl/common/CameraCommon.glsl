// CameraCommon.glsl - the camera SSBO layout
// The C++ CameraGPUData struct in renderer/GPUDataStructs.h must match this exactly.

#ifndef CAMERA_COMMON_GLSL
#define CAMERA_COMMON_GLSL

struct CameraGPUData {
    mat4 view;
    mat4 proj;
    mat4 invViewProj;
    mat4 prevViewProj;
};

layout(std430, set = 0, binding = 0) readonly buffer CameraDataSSBO {
    CameraGPUData cameras[];
} u_cameraSSBO[];

// Screen-space motion of a static world position, in uv units. A consumer reprojects into the
// previous frame with uv - motion
vec2 cameraMotionUV(CameraGPUData _cam, vec3 _worldPos) {
    vec4 currClip = _cam.proj * _cam.view * vec4(_worldPos, 1.0);
    vec4 prevClip = _cam.prevViewProj * vec4(_worldPos, 1.0);
    vec2 currNdc = currClip.xy / currClip.w;
    vec2 prevNdc = prevClip.xy / prevClip.w;
    return (currNdc - prevNdc) * 0.5;
}

#endif // CAMERA_COMMON_GLSL

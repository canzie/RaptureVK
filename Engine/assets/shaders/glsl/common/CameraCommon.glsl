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

// Positive distance in front of the camera for a depth buffer value. Derived from the projection
// matrix itself, so it holds for any near/far split, forward or reverse Z.
// _depth is NDC z as the depth buffer stores it, which under Vulkan's [0, 1] clip range is the
// value read straight back out. A GL-style [-1, 1] NDC z has to be remapped by the caller first.
float cameraLinearDepth(CameraGPUData _cam, float _depth) {
    return -_cam.proj[3][2] / (_depth * _cam.proj[2][3] - _cam.proj[2][2]);
}

// The inverse of cameraLinearDepth. A ray is a straight line in NDC but not in linear depth, so a
// walk that steps in screen space has to bring stored linear depths back into NDC to compare.
float cameraNdcDepthFromLinear(CameraGPUData _cam, float _linearDepth) {
    return (_cam.proj[3][2] - _cam.proj[2][2] * _linearDepth) / max(_linearDepth, 1e-6);
}

// The view space position of a pixel, from its uv and its positive distance in front of the camera.
// Assumes a projection with no skew, so the two diagonal terms alone undo it.
vec3 cameraViewPositionFromLinearDepth(CameraGPUData _cam, vec2 _uv, float _linearDepth) {
    vec2 ndc = _uv * 2.0 - 1.0;
    return vec3(ndc.x * _linearDepth / _cam.proj[0][0], ndc.y * _linearDepth / _cam.proj[1][1], -_linearDepth);
}

#endif // CAMERA_COMMON_GLSL

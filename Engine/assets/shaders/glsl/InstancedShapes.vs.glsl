#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec4 outColor;


layout(push_constant) uniform PushConstants {
    mat4 globalTransform;
    vec4 color;
    uint cameraUBOIndex;
    uint instanceDataSSBOIndex;
} pc;


struct InstanceData {
    mat4 transform;
};

// Camera data
layout(set = 0, binding = 0) uniform CameraDataBuffer {
    mat4 view;
    mat4 proj;
} u_camera[]; // each index is for a different frame, all the same camera

// Bindless array of SSBOs
layout(set = 3, binding = 1) readonly buffer InstanceDataSSBO {
    uint data[];
} instanceSSBOs[];

mat4 getInstanceTransform(uint instanceIdx, uint ssboIdx) {
    uint base_offset = instanceIdx * 16; // 16 floats in a mat4
    
    vec4 c0 = vec4(uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 0]),
                   uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 1]),
                   uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 2]),
                   uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 3]));
    vec4 c1 = vec4(uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 4]),
                   uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 5]),
                   uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 6]),
                   uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 7]));
    vec4 c2 = vec4(uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 8]),
                   uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 9]),
                   uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 10]),
                   uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 11]));
    vec4 c3 = vec4(uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 12]),
                   uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 13]),
                   uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 14]),
                   uintBitsToFloat(instanceSSBOs[ssboIdx].data[base_offset + 15]));

    return mat4(c0, c1, c2, c3);
}

void main() {
    mat4 instanceTransform = getInstanceTransform(gl_InstanceIndex, pc.instanceDataSSBOIndex);
    
    outColor = pc.color; // Pass color to fragment shader

    mat4 modelMatrix = pc.globalTransform * instanceTransform;
    gl_Position = u_camera[pc.cameraUBOIndex].proj * u_camera[pc.cameraUBOIndex].view * modelMatrix * vec4(inPosition, 1.0);
} 
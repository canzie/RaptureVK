#version 450

#extension GL_EXT_multiview : require
#extension GL_EXT_nonuniform_qualifier : require


// Uniform buffer containing light view-projection matrices for all cascades
layout(set = 0, binding = 3) uniform CascadeMatrices {
    mat4 lightViewProjection[4];  // Support up to 4 cascades
} u_cascades[];

// Object info structure
struct ObjectInfo {
    uint meshIndex;
    uint materialIndex;
};

// Batch info buffer containing per-object data
layout(set = 0, binding = 6) readonly buffer BatchInfoBuffer {
    ObjectInfo objects[];
} u_batchInfo[];


// Mesh data buffer containing transform matrices
layout(set = 2, binding = 0) uniform MeshDataBuffer {
    mat4 modelMatrix;
    uint flags;
} u_meshData[];

// Push constants
layout(push_constant) uniform PushConstants {
    uint shadowMatrixIndices;
    uint batchInfoBufferIndex;
} pc;

// Vertex attributes
layout(location = 0) in vec3 inPosition;

void main() {
    // gl_ViewIndex is automatically provided by the multiview extension
    // It corresponds to which cascade/view we're rendering to (0, 1, 2, or 3)
    
    // Get batch info for this draw call using gl_InstanceIndex
    uint meshBufferIndex = u_batchInfo[pc.batchInfoBufferIndex].objects[gl_InstanceIndex].meshIndex;
    
    // Get transform matrix from mesh data buffer
    mat4 modelMatrix = u_meshData[meshBufferIndex].modelMatrix;
    
    // Transform vertex position to world space, then to light space for the current cascade
    vec4 worldPosition = modelMatrix * vec4(inPosition, 1.0);
    gl_Position = u_cascades[pc.shadowMatrixIndices].lightViewProjection[gl_ViewIndex] * worldPosition;
} 
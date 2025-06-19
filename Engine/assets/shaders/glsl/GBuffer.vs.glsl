#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

layout(location = 0) out vec4 outFragPosDepth;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec3 outBitangent;
layout(location = 5) out flat uint outFlags;



layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;



// Push constant for per-object data
layout(push_constant) uniform PushConstants {
    mat4 model;
    uint flags; // Bit flags for vertex attribute availability
} pushConstants;

// Bit flag definitions
const uint FLAG_HAS_NORMALS = 1u;
const uint FLAG_HAS_TANGENTS = 2u;
const uint FLAG_HAS_BITANGENTS = 4u;
const uint FLAG_HAS_TEXCOORDS = 8u;
const uint FLAG_HAS_NORMAL_MAP = 16u;

void main() {

    // Use flags to determine attribute availability (branchless)
    float hasNormals = float((pushConstants.flags & FLAG_HAS_NORMALS) != 0u);
    float hasTangents = float((pushConstants.flags & FLAG_HAS_TANGENTS) != 0u);
    float hasBitangents = float((pushConstants.flags & FLAG_HAS_BITANGENTS) != 0u);
    float hasTexcoords = float((pushConstants.flags & FLAG_HAS_TEXCOORDS) != 0u);

    // Transform to world space
    outFragPosDepth.xyz = vec3(pushConstants.model * vec4(aPosition, 1.0));
    
    // Handle normals branchlessly
    vec3 defaultNormal = vec3(0.0, 1.0, 0.0);
    vec3 transformedNormal = normalize(mat3(pushConstants.model) * aNormal);
    outNormal = mix(defaultNormal, transformedNormal, hasNormals);
    
    // Handle tangents branchlessly
    vec3 defaultTangent = vec3(1.0, 0.0, 0.0);
    vec3 transformedTangent = normalize(mat3(pushConstants.model) * aTangent.xyz);
    outTangent = mix(defaultTangent, transformedTangent, hasTangents);
    
    // Calculate bitangent using glTF convention with handedness from tangent.w
    vec3 calculatedBitangent = cross(aNormal, aTangent.xyz) * aTangent.w;
    vec3 transformedBitangent = normalize(mat3(pushConstants.model) * calculatedBitangent);
    vec3 defaultBitangent = vec3(0.0, 0.0, 1.0);
    outBitangent = mix(defaultBitangent, transformedBitangent, hasBitangents * hasTangents * hasNormals);

    // Re-orthogonalize tangent with respect to normal when both are available
    vec3 orthogonalizedTangent = normalize(outTangent - dot(outTangent, outNormal) * outNormal);
    outTangent = mix(outTangent, orthogonalizedTangent, hasNormals * hasTangents);
    
    // Recalculate bitangent to ensure orthogonal basis, preserving handedness
    vec3 recalculatedBitangent = cross(outNormal, outTangent) * sign(dot(outBitangent, cross(outNormal, outTangent)));
    outBitangent = mix(outBitangent, recalculatedBitangent, hasNormals * hasTangents * hasBitangents);

    // Handle texture coordinates branchlessly
    vec2 defaultTexCoord = vec2(0.0, 0.0);
    outTexCoord = mix(defaultTexCoord, aTexCoord, hasTexcoords);
    
    // Pass flags to fragment shader
    outFlags = pushConstants.flags;
    
    // Calculate position in view space
    vec4 viewPos = ubo.view * vec4(outFragPosDepth.xyz, 1.0);

    // Store the negative Z value (common convention, depth increases into the screen)
    // Ensure this matches how cascade splits are calculated on the CPU.
    // If cascade splits are positive distances, use abs(viewPos.z) or just viewPos.z
    outFragPosDepth.w = -viewPos.z;

    // Final clip space position
    gl_Position = ubo.proj * viewPos; // Use viewPos directly for projection
}
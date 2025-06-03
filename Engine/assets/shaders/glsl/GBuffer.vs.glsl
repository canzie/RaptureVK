#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aTangent;

layout(location = 0) out vec4 outFragPosDepth;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec3 outBitangent;



layout(std140, binding = 0) uniform BaseTransformMats {
	mat4 u_proj;
	mat4 u_view;
};

layout(std140, binding = 6) uniform BoneMatrices {
    mat4 u_BoneTransforms[100]; // Array of bone transforms
};

// Push constant for per-object data
layout(push_constant) uniform PushConstants {
    mat4 model;
} pushConstants;

void main() {


    // Transform to world space
    outFragPosDepth.xyz = vec3(pushConstants.model * vec4(aPosition, 1.0));
    outNormal = normalize(mat3(pushConstants.model) * aNormal);
    outTangent = normalize(mat3(pushConstants.model) * aTangent);
    outBitangent = normalize(mat3(pushConstants.model) * cross(aNormal, aTangent));
    

    // Re-orthogonalize tangent with respect to normal
    outTangent = normalize(outTangent - dot(outTangent, outNormal) * outNormal);
    // Recalculate bitangent to ensure orthogonal basis
    outBitangent = cross(outNormal, outTangent);


    outTexCoord = aTexCoord;
    
    // Calculate position in view space
    vec4 viewPos = u_view * vec4(outFragPosDepth.xyz, 1.0);

    // Store the negative Z value (common convention, depth increases into the screen)
    // Ensure this matches how cascade splits are calculated on the CPU.
    // If cascade splits are positive distances, use abs(viewPos.z) or just viewPos.z
    outFragPosDepth.w = -viewPos.z;

    // Final clip space position
    gl_Position = u_proj * viewPos; // Use viewPos directly for projection
}
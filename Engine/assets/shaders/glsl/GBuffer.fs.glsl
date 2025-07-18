#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) out vec4 gPositionDepth; // Renamed and changed to vec4
layout(location = 1) out vec4 gNormal; // Changed from vec3 to vec4
layout(location = 2) out vec4 gAlbedoSpec;
layout(location = 3) out vec4 gMaterial; // R: Metallic, G: Roughness, B: AO - Changed to vec4

layout(location = 0) in vec4 inFragPosDepth;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in flat uint inFlags; // Receive flags from vertex shader
layout(location = 6) in flat uint inMaterialIndex;

precision highp float;

// Bit flag definitions (must match vertex shader)
const uint FLAG_HAS_NORMALS = 1u;
const uint FLAG_HAS_TANGENTS = 2u;
const uint FLAG_HAS_BITANGENTS = 4u;
const uint FLAG_HAS_TEXCOORDS = 8u;
const uint FLAG_HAS_ALBEDO_MAP = 32u;
const uint FLAG_HAS_NORMAL_MAP = 64u;
const uint FLAG_HAS_METALLIC_ROUGHNESS_MAP = 128u;
const uint FLAG_HAS_AO_MAP = 256u;



// Material data
layout(set = 1, binding = 0) uniform MaterialDataBuffer {
    vec3 albedo;
    float roughness;
    float metallic;
    uint albedoMapIndex;
    uint normalMapIndex;
    uint metallicRoughnessMapIndex;
    uint aoMapIndex;
    vec3 emissiveColor;

} u_materials[];

// Bindless textures
layout(set = 3, binding = 0) uniform sampler2D u_textures[];

// Push constants
layout(push_constant) uniform PushConstants {
    uint batchInfoBufferIndex;
    uint cameraBindlessIndex; 
} pc;

void main() {

    // Get material index from batch info using gl_InstanceIndex
    uint materialIndex = inMaterialIndex;

    // Use flags to determine attribute availability (branchless)
    float hasNormals = float((inFlags & FLAG_HAS_NORMALS) != 0u);
    float hasTangents = float((inFlags & FLAG_HAS_TANGENTS) != 0u);
    float hasBitangents = float((inFlags & FLAG_HAS_BITANGENTS) != 0u);
    float hasTexcoords = float((inFlags & FLAG_HAS_TEXCOORDS) != 0u);
    float hasNormalMap = float((inFlags & FLAG_HAS_NORMAL_MAP) != 0u);
    float hasAlbedoMap = float((inFlags & FLAG_HAS_ALBEDO_MAP) != 0u);
    float hasMetallicRoughnessMap = float((inFlags & FLAG_HAS_METALLIC_ROUGHNESS_MAP) != 0u);
    float hasAoMap = float((inFlags & FLAG_HAS_AO_MAP) != 0u);

    // Handle texture coordinates branchlessly
    vec2 texCoord = mix(vec2(0.0, 0.0), inTexCoord, hasTexcoords);

    // Get material properties from textures or fallback to uniforms
    float roughness = u_materials[materialIndex].roughness;
    float metallic = u_materials[materialIndex].metallic;
    float ao = 1.0;

    // Albedo with conditional texture sampling
    vec3 albedoTexture = texture(u_textures[u_materials[materialIndex].albedoMapIndex], texCoord).rgb;
    vec3 albedo = mix(u_materials[materialIndex].albedo, albedoTexture * u_materials[materialIndex].albedo, hasAlbedoMap);

    // Roughness with conditional texture sampling
    float roughnessTexture = texture(u_textures[u_materials[materialIndex].metallicRoughnessMapIndex], texCoord).g;
    roughness = mix(roughness, roughness * roughnessTexture, hasMetallicRoughnessMap);
    
    // Metallic with conditional texture sampling
    float metallicTexture = texture(u_textures[u_materials[materialIndex].metallicRoughnessMapIndex], texCoord).b;
    metallic = mix(metallic, metallic * metallicTexture, hasMetallicRoughnessMap);

    // AO with conditional texture sampling
    float aoTexture = texture(u_textures[u_materials[materialIndex].aoMapIndex], texCoord).r;
    ao = mix(ao, ao * aoTexture, hasAoMap);

    // Position (world space) and Depth (view space Z)
    gPositionDepth = inFragPosDepth;
    
    // Normal calculation with efficient branching
    vec3 normal;
    
    if ((inFlags & FLAG_HAS_NORMAL_MAP) != 0u && (inFlags & FLAG_HAS_TEXCOORDS) != 0u) {
        // We have a normal map and texture coordinates
        vec3 tangentNormal = texture(u_textures[u_materials[materialIndex].normalMapIndex], texCoord).xyz * 2.0 - 1.0;
        
        if ((inFlags & FLAG_HAS_TANGENTS) != 0u && (inFlags & FLAG_HAS_BITANGENTS) != 0u) {
            // Use pre-computed TBN matrix (optimal path)
            vec3 N = normalize(inNormal);
            vec3 T = normalize(inTangent);
            vec3 B = normalize(inBitangent);
            mat3 TBN = mat3(T, B, N);
            normal = normalize(TBN * tangentNormal);
        } else if ((inFlags & FLAG_HAS_NORMALS) != 0u) {
            // Fallback to derivative-based TBN
            vec3 Q1 = dFdx(inFragPosDepth.xyz);
            vec3 Q2 = dFdy(inFragPosDepth.xyz);
            vec2 st1 = dFdx(texCoord);
            vec2 st2 = dFdy(texCoord);
            vec3 N = normalize(inNormal);
            vec3 T = normalize(Q1 * st2.t - Q2 * st1.t);
            vec3 B = normalize(cross(N, T));
            mat3 TBN = mat3(T, B, N);
            normal = normalize(TBN * tangentNormal);
        } else {
            // No normals available, use default
            normal = vec3(0.0, 1.0, 0.0);
        }
    } else {
        // Use vertex normals directly
        normal = normalize(inNormal);
    }

    gNormal = vec4(normal, 1.0);
    
    // Albedo and specular
    gAlbedoSpec = vec4(albedo, 1.0);
    
    // Material properties
    gMaterial = vec4(metallic, roughness, ao, 1.0); // Output vec4 with alpha = 1.0
}
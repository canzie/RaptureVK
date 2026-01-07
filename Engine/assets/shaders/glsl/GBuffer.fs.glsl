#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

#include "common/MaterialCommon.glsl"

layout(location = 0) out vec4 gPositionDepth;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedoSpec;
layout(location = 3) out vec4 gMaterial;

layout(location = 0) in vec4 inFragPosDepth;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in flat uint inFlags;
layout(location = 6) in flat uint inMaterialIndex;

precision highp float;

layout(set = 1, binding = 0) uniform MaterialDataBuffer {
    MaterialData data;
} u_materials[];

layout(set = 3, binding = 0) uniform sampler2D u_textures[];

layout(push_constant) uniform PushConstants {
    uint batchInfoBufferIndex;
    uint cameraBindlessIndex;
} pc;

void main() {
    uint materialIndex = inMaterialIndex;
    MaterialData mat = u_materials[materialIndex].data;
    uint flags = mat.flags | inFlags;

    float hasTexcoords = matFlagMul(flags, MAT_FLAG_HAS_TEXCOORDS);
    vec2 texCoord = mix(vec2(0.0), inTexCoord, hasTexcoords);

    vec3 albedo = SAMPLE_ALBEDO(mat, u_textures, texCoord);
    float roughness = SAMPLE_ROUGHNESS(mat, u_textures, texCoord);
    float metallic = SAMPLE_METALLIC(mat, u_textures, texCoord);
    float ao = SAMPLE_AO(mat, u_textures, texCoord);

    gPositionDepth = inFragPosDepth;

    vec3 normal;
    if (matHasFlag(flags, MAT_FLAG_HAS_NORMAL_MAP) && matHasFlag(flags, MAT_FLAG_HAS_TEXCOORDS)) {
        vec3 tangentNormal = SAMPLE_NORMAL_MAP(mat, u_textures, texCoord);

        if (matHasFlag(flags, MAT_FLAG_HAS_TANGENTS) && matHasFlag(flags, MAT_FLAG_HAS_BITANGENTS)) {
            vec3 N = normalize(inNormal);
            vec3 T = normalize(inTangent);
            vec3 B = normalize(inBitangent);
            mat3 TBN = mat3(T, B, N);
            normal = normalize(TBN * tangentNormal);
        } else if (matHasFlag(flags, MAT_FLAG_HAS_NORMALS)) {
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
            normal = vec3(0.0, 1.0, 0.0);
        }
    } else {
        normal = normalize(inNormal);
    }

    gNormal = vec4(normal, 1.0);
    gAlbedoSpec = vec4(albedo, 1.0);
    gMaterial = vec4(metallic, roughness, ao, 1.0);
}

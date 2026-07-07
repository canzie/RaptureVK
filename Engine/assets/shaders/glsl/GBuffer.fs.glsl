#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

#include "common/MaterialCommon.glsl"
#include "generated/SurfaceGraphs.glsl"

layout(location = 0) out vec2 gNormal;
layout(location = 1) out vec4 gAlbedoSpec;
layout(location = 2) out vec4 gMaterial;

layout(location = 0) in vec4 inFragPosDepth;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in flat uint inFlags;
layout(location = 6) in flat uint inMaterialIndex;

precision highp float;

layout(set = 3, binding = 0) uniform sampler2D u_textures[];

layout(push_constant) uniform PushConstants {
    uint batchInfoBufferIndex;
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint meshSSBOIndex;
} pc;

SurfaceData evalStaticSurface(SurfaceInputs si, MaterialData mat) {
    SurfaceData surf;
    surf.albedo = SAMPLE_ALBEDO(mat, u_textures, si.uv);
    surf.roughness = SAMPLE_ROUGHNESS(mat, u_textures, si.uv);
    surf.metallic = SAMPLE_METALLIC(mat, u_textures, si.uv);
    surf.ao = SAMPLE_AO(mat, u_textures, si.uv);
    surf.shadingModelId = SM_OPENPBR_STANDARD;

    if (matHasFlag(si.flags, MAT_FLAG_HAS_NORMAL_MAP) && matHasFlag(si.flags, MAT_FLAG_HAS_TEXCOORDS)) {
        vec3 tangentNormal = SAMPLE_NORMAL_MAP(mat, u_textures, si.uv);

        if (matHasFlag(si.flags, MAT_FLAG_HAS_TANGENTS) && matHasFlag(si.flags, MAT_FLAG_HAS_BITANGENTS)) {
            vec3 N = normalize(si.worldNormal);
            vec3 T = normalize(si.tangent);
            vec3 B = normalize(si.bitangent);
            mat3 TBN = mat3(T, B, N);
            surf.normal = normalize(TBN * tangentNormal);
        } else if (matHasFlag(si.flags, MAT_FLAG_HAS_NORMALS)) {
            vec3 Q1 = dFdx(si.worldPos);
            vec3 Q2 = dFdy(si.worldPos);
            vec2 st1 = dFdx(si.uv);
            vec2 st2 = dFdy(si.uv);
            vec3 N = normalize(si.worldNormal);
            vec3 T = normalize(Q1 * st2.t - Q2 * st1.t);
            vec3 B = normalize(cross(N, T));
            mat3 TBN = mat3(T, B, N);
            surf.normal = normalize(TBN * tangentNormal);
        } else {
            surf.normal = vec3(0.0, 1.0, 0.0);
        }
    } else {
        surf.normal = normalize(si.worldNormal);
    }

    return surf;
}

void main() {
    MaterialData mat = getMaterialData(inMaterialIndex);
    uint flags = mat.flags | inFlags;

    SurfaceInputs si;
    si.uv = mix(vec2(0.0), inTexCoord, matFlagMul(flags, MAT_FLAG_HAS_TEXCOORDS));
    si.worldPos = inFragPosDepth.xyz;
    si.worldNormal = inNormal;
    si.tangent = inTangent;
    si.bitangent = inBitangent;
    si.flags = flags;

    SurfaceData surf = matHasFlag(flags, MAT_FLAG_IS_GRAPH)
        ? evalSurfaceGraph(mat.graphId, si, mat.graphDataOffset)
        : evalStaticSurface(si, mat);

    gNormal = octEncodeNormal(normalize(surf.normal));
    gAlbedoSpec = vec4(surf.albedo, 1.0);
    gMaterial = vec4(surf.metallic, surf.roughness, surf.ao, packShadingModel(surf.shadingModelId));
}

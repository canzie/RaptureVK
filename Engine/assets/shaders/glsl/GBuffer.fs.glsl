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

layout(push_constant) uniform PushConstants {
    uint batchInfoBufferIndex;
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint meshSSBOIndex;
} pc;

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

    SurfaceData surf = evalSurfaceGraph(mat.graphId, si, mat.graphDataOffset);

    gNormal = octEncodeNormal(normalize(surf.normal));
    gAlbedoSpec = vec4(surf.albedo, 1.0);
    gMaterial = vec4(surf.metallic, surf.roughness, surf.ao, packShadingModel(surf.shadingModelId));
}

#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

#include "common/CameraCommon.glsl"
#include "common/GBufferOutput.glsl"
#include "generated/SurfaceGraphs.glsl"

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

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];

    GBufferSurface gs = defaultGBufferSurface();
    gs.normal = surf.normal;
    gs.motion = cameraMotionUV(cam, inFragPosDepth.xyz);
    gs.baseColor = surf.albedo;
    gs.emissiveIntensity = surf.emissiveStrength * LinearRGBToLuminance(surf.emission.rgb);
    gs.metallic = surf.metallic;
    gs.roughness = surf.roughness;
    gs.ao = surf.ao;
    gs.specular = surf.specular;
    gs.shadingModelId = surf.shadingModelId;
    writeGBuffer(gs);
}

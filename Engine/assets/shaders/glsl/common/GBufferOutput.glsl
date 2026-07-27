// GBufferOutput.glsl - the G-buffer attachment layout and the single writer for it

#ifndef GBUFFER_OUTPUT_GLSL
#define GBUFFER_OUTPUT_GLSL

#include "common/MaterialCommon.glsl"

layout(location = 0) out vec4 gNormalMotion;
layout(location = 1) out vec4 gBaseColor;
layout(location = 2) out vec4 gMaterial;
layout(location = 3) out vec4 gShadingModel;

// The surface as the G-buffer stores it: the evaluated response, not the authoring parameters.
// customData is reinterpreted per shadingModelId, see the shading model table
struct GBufferSurface {
    vec3 normal;
    vec2 motion;
    vec3 baseColor;
    float emissiveIntensity;
    float metallic;
    float roughness;
    float ao;
    float specular;
    uint shadingModelId;
    vec3 customData;
};

GBufferSurface defaultGBufferSurface() {
    GBufferSurface s;
    s.normal = vec3(0.0, 1.0, 0.0);
    s.motion = vec2(0.0);
    s.baseColor = vec3(1.0);
    s.emissiveIntensity = 0.0;
    s.metallic = 0.0;
    s.roughness = 0.5;
    s.ao = 1.0;
    s.specular = 0.04;
    s.shadingModelId = SM_OPENPBR_STANDARD;
    s.customData = vec3(0.0);
    return s;
}

void writeGBuffer(GBufferSurface s) {
    gNormalMotion = vec4(octEncodeNormal(normalize(s.normal)), s.motion);
    gBaseColor = vec4(s.baseColor, packEmissive(s.emissiveIntensity));
    gMaterial = vec4(s.metallic, s.roughness, s.ao, packSpecular(s.specular));
    gShadingModel = vec4(packShadingModel(s.shadingModelId), s.customData);
}

#endif // GBUFFER_OUTPUT_GLSL

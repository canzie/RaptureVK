// GBufferOutput.glsl - the G-buffer attachment layout and the single writer for it

#ifndef GBUFFER_OUTPUT_GLSL
#define GBUFFER_OUTPUT_GLSL

#include "common/MaterialCommon.glsl"

layout(location = 0) out vec4 gNormalMotion;
layout(location = 1) out vec4 gBaseColor;
layout(location = 2) out vec4 gMaterial;
layout(location = 3) out vec4 gShadingModel;

// Attachments past the guaranteed four exist only under GBUFFER_ATTACHMENT_COUNT_ALL. A device that
// cannot afford them loses the feature outright, so there is no fallback path to write here.
#ifdef GBUFFER_ATTACHMENT_COUNT_ALL
layout(location = 4) out uint gEntityId;

// Ids are stored biased by one so the cleared value, zero, reads back as nothing drawn. Every
// fragment must write this slot, since an output left untouched is undefined.
void writeGBufferEntityId(uint _entityId) {
    gEntityId = _entityId + 1u;
}

void writeGBufferNoEntityId() {
    gEntityId = 0u;
}
#endif // GBUFFER_ATTACHMENT_COUNT_ALL

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

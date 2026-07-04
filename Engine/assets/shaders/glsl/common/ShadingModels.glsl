// ShadingModels.glsl - shading model ids + G-buffer packing, shared by the G-buffer and lighting passes

#ifndef SHADING_MODELS_GLSL
#define SHADING_MODELS_GLSL

const uint SM_UNLIT            = 0u;
const uint SM_OPENPBR_STANDARD = 1u;

// Pack a shading-model id into a UNORM8 channel
float packShadingModel(uint id) {
    return float(id) / 255.0;
}

// Recover a shading-model id from a UNORM8 channel
uint unpackShadingModel(float packed) {
    return uint(packed * 255.0 + 0.5);
}

#endif // SHADING_MODELS_GLSL

// ShadingModels.glsl - shading model ids + G-buffer packing, shared by the G-buffer and lighting passes

#ifndef SHADING_MODELS_GLSL
#define SHADING_MODELS_GLSL

const uint SM_UNLIT            = 0u;
const uint SM_OPENPBR_STANDARD = 1u;

// Pack a shading-model id into a UNORM8 channel
float packShadingModel(uint _id) {
    return float(_id) / 255.0;
}

// Recover a shading-model id from a UNORM8 channel
uint unpackShadingModel(float _packed) {
    return uint(_packed * 255.0 + 0.5);
}

// Dielectric F0 over a UNORM8 channel. The [0, 0.125] range covers ior 1.0-2.0, with the 1.5
// default landing on 0.04
float packSpecular(float _f0) {
    return clamp(_f0 * 8.0, 0.0, 1.0);
}

float unpackSpecular(float _packed) {
    return _packed * 0.125;
}

// Emissive intensity over a UNORM8 channel, log2 encoded for a 2^-8 .. 2^8 HDR range. Zero is
// reserved for "not emissive", so the smallest representable intensity is one step above it
float packEmissive(float _intensity) {
    if (_intensity <= 0.0) {
        return 0.0;
    }
    return clamp((log2(_intensity) + 8.0) / 16.0, 1.0 / 255.0, 1.0);
}

float unpackEmissive(float _packed) {
    if (_packed <= 0.0) {
        return 0.0;
    }
    return exp2(_packed * 16.0 - 8.0);
}

#endif // SHADING_MODELS_GLSL

// BRDF.glsl - OpenPBR-standard direct lighting BRDF

#ifndef BRDF_GLSL
#define BRDF_GLSL

#include "common/ShadingModels.glsl"

#ifndef PI
#define PI 3.14159265359
#endif

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Roughness-aware Fresnel for ambient / IBL specular (Lagarde). Rough surfaces
// keep a dimmer grazing response than the mirror-smooth fresnelSchlick would give.
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    vec3 Fr = max(vec3(1.0 - roughness), F0);
    return F0 + (Fr - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX / Trowbridge-Reitz normal distribution
float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (PI * d * d);
}

// Height-correlated Smith visibility (folds in the 1 / (4 NdotV NdotL) denominator)
float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

// OpenPBR standard surface, single scatter. Returns BRDF * NdotL (no light color/intensity)
vec3 evalStandardBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) {
        return vec3(0.0);
    }

    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlick(VdotH, F0);
    float D = D_GGX(NdotH, roughness);
    float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);

    vec3 specular = D * Vis * F;

    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kd * albedo / PI;

    return (diffuse + specular) * NdotL;
}

// Dispatch by shading model. Returns BRDF * NdotL
vec3 evalBRDF(uint shadingModelId, vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness) {
    if (shadingModelId == SM_OPENPBR_STANDARD) {
        return evalStandardBRDF(N, V, L, albedo, metallic, roughness);
    }
    return vec3(0.0);
}

#endif // BRDF_GLSL

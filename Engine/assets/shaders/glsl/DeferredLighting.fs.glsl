#version 450
#extension GL_EXT_nonuniform_qualifier : require

#ifndef PROBE_OFFSETS_TEXTURE
#define PROBE_OFFSETS_TEXTURE
#endif



layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 fragTexCoord;


// Light types
#define LIGHT_TYPE_POINT       0
#define LIGHT_TYPE_DIRECTIONAL 1
#define LIGHT_TYPE_SPOT        2

#define MAX_CASCADES 4

// Add a debug mode flag at the top
#define DEBUG_SPOTLIGHTS 0

#define DEBUG_CASCADES 0
#define DEBUG_DIRECTIONAL_SHADOWS 0  // Set to 1 to enable debugging
#define DEBUG_SHADOW_COORDS 0
#define USE_PCF 0

#define CASCADE_BLEND_WIDTH_PERCENT 0.15


layout(set = 3, binding = 0) uniform sampler2D gTextures[];
layout(set = 3, binding = 0) uniform sampler2DShadow gShadowTextures[];
layout(set = 3, binding = 0) uniform sampler2DArrayShadow gShadowArrays[];
layout(set = 3, binding = 0) uniform sampler2DArray gTextureArrays[];
layout(set = 3, binding = 0) uniform usampler2DArray gUintTextureArrays[];

#include "ProbeCommon.glsl"
#include "IrradianceCommon.glsl"


// Light data structure for shader
struct LightData {
    vec4 position;      // w = light type (0 = point, 1 = directional, 2 = spot)
    vec4 direction;     // w = range
    vec4 color;         // w = intensity
    vec4 spotAngles;    // x = inner cone cos, y = outer cone cos, z = entity id, w = unused
};


struct ShadowBufferData {
    int type; // 0 = point, 1 = directional, 2 = spot
    uint cascadeCount;
    uint lightIndex; // Index of the light this shadow maps to
    uint textureHandle;
    mat4 cascadeMatrices[MAX_CASCADES];
    vec4 cascadeSplitsViewSpace[MAX_CASCADES]; // Contains view-space Z split depths in .x component
};

layout(std140, set = 0, binding = 1) uniform LightDataBuffer {
    LightData lightData;
} u_lightData[];


layout(std140, set = 0, binding = 4) uniform ShadowDataBuffer {
    ShadowBufferData shadowData;
} u_shadowData[];


layout(std140, set = 0, binding = 5) uniform ProbeInfo {
    ProbeVolume u_DDGI_Volume;
};


layout(push_constant) uniform PushConstants {
    vec4 cameraPos;
    uint lightCount;
    uint shadowCount;
    
    uint GBufferAlbedoHandle;
    uint GBufferNormalHandle;
    uint GBufferPositionHandle;
    uint GBufferMaterialHandle;
    uint GBufferDepthHandle;

    uint useDDGI;
    uint probeVolumeHandle;
    uint probeIrradianceHandle;
    uint probeVisibilityHandle;
    uint probeOffsetHandle;
    uint probeClassificationHandle;

    // Fog
    vec4 fogColor;     // .rgb = color, .a = enabled
    vec2 fogDistances; // .x = near, .y = far
} pc;

float exposure(float fstop) {
    return pow(2.0, fstop);
}

vec3 ACESFilm(vec3 color) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((color*(a*color + b)) / (color*(c*color + d) + e), vec3(0.0), vec3(1.0));
}

vec3 LessThan(vec3 f, float value)
{
    return vec3(
        (f.x < value) ? 1.0 : 0.0,
        (f.y < value) ? 1.0 : 0.0,
        (f.z < value) ? 1.0 : 0.0);
}

vec3 pow3(vec3 x, float y) {
    return vec3(pow(x.x, y), pow(x.y, y), pow(x.z, y));
}

vec3 LinearToSRGB(vec3 rgb)
{
    rgb = clamp(rgb, 0.0, 1.0);
    return mix(
        pow3(rgb * 1.055, 1.0 / 2.4) - 0.055,
        rgb * 12.92,
        LessThan(rgb, 0.0031308)
    );
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159265 * denom * denom;
    
    return num / denom;
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
    
    return ggx1 * ggx2;
}


float calculateAttenuation(vec3 lightPos, vec3 fragPos, float range) {
    float distance = length(lightPos - fragPos);
    float attenuation = 1.0;
    
    float rangeSquared = range * range;
    attenuation = clamp(1.0 - (distance * distance) / rangeSquared, 0.0, 1.0);
    attenuation *= attenuation; // Apply squared falloff for smoother transition
    
    
    return attenuation;
}

float SpotAttenuation(vec3 spotDirection, vec3 lightDirection, float umbra, float penumbra)
{
    // Spot attenuation function from Frostbite, pg 115 in RTR4
    float cosTheta = clamp(dot(spotDirection, lightDirection), 0.0, 1.0);
    float t = clamp((cosTheta - cos(umbra)) / (cos(penumbra) - cos(umbra)), 0.0, 1.0);
    return t * t;
}

float LightFalloff(float distanceToLight) {
    return 1.0 / pow(max(distanceToLight, 1.0), 2);
}

float LightWindowing(float distanceToLight, float maxDistance) {
    return pow(clamp(1.0 - pow((distanceToLight / maxDistance), 4), 0.0, 1.0), 2);
}



vec3 calculateLightContribution(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, float ao, vec3 lightColor, float intensity) {
    vec3 H = normalize(V + L);
    
    float NdotL = max(dot(N, L), 0.0);        


    if (NdotL <= 0.0) return vec3(0.0);

    // Calculate F0 (surface reflection at zero incidence)
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Specular BRDF terms
    float NdotV = max(dot(N, V), 0.0001);
    float NDF = distributionGGX(N, H, roughness);       
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    // Calculate specular term
    vec3  numerator = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL;
    vec3  specular = numerator / max(denominator, 0.0001);
    

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    return (kD * albedo / 3.14159265359 + specular) * lightColor * intensity * NdotL;
}

// Helper function to calculate shadow for a specific cascade - this contains the PCF shadow mapping logic
float calculateShadowForCascade(vec3 fragPosWorld, vec3 normal, vec3 lightDir, ShadowBufferData shadowInfo, 
                              mat4 lightMatrix, int cascadeIndex) {
    // Transform fragment position from world space to light clip space
    vec4 fragPosLightSpace = lightMatrix * vec4(fragPosWorld, 1.0);

    // Perform perspective divide (clip space -> NDC [-1, 1])
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // Transform to [0,1] range (NDC -> UV coordinates for texture lookup)
    // In Vulkan, projCoords.z is already in the [0, 1] range.
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    
#if DEBUG_SHADOW_COORDS
    // Debug: Visualize shadow coordinates for directional lights
    if (shadowInfo.type == LIGHT_TYPE_DIRECTIONAL) { // Directional light
        // Show coordinates as colors
        vec3 debugColor = vec3(projCoords.xy, projCoords.z * 0.1);
        return length(debugColor); // Return a debug value
    }
#endif
    
    if(projCoords.x < 0.0 || projCoords.x > 1.0 || 
       projCoords.y < 0.0 || projCoords.y > 1.0 ||
       projCoords.z < 0.0 || projCoords.z > 1.0) { // Check Z too
        
#if DEBUG_DIRECTIONAL_SHADOWS
        // Debug: Show fragments outside shadow frustum as red for directional lights
        if (shadowInfo.type == LIGHT_TYPE_DIRECTIONAL) {
            return 0.0; /
        }
#endif
        return 1.0; 
    }
    
    float shadowFactor = 0.0;
    vec2 texelSize;
    float bias;
    float samples;

    if (shadowInfo.cascadeCount > 1) {

        texelSize = 1.0 / vec2(textureSize(gShadowArrays[shadowInfo.textureHandle], 0));

        // Apply bias to avoid shadow acne
        float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);
        // Progressively reduce bias for farther cascades to reduce light leaking
        float cascadeBiasMultiplier = 1.0 / (1.0 + float(cascadeIndex) * 0.5);

        float distanceScale = 1.0;
        if (shadowInfo.type == LIGHT_TYPE_SPOT) { // Spotlight
            float viewDepth = abs(fragPosLightSpace.z);
            distanceScale = mix(1.0, 3.0, clamp(viewDepth / 50.0, 0.0, 1.0));
        }else if (shadowInfo.type == LIGHT_TYPE_DIRECTIONAL) { // Directional light
            distanceScale = 0.5;
        }


        bias = max(0.005 * (1.0 - cosTheta) * distanceScale * cascadeBiasMultiplier, 0.0005);
        float comparisonDepth = projCoords.z - bias;

#if USE_PCF
        const int kernelRadius = 3; // 7x7 kernel → radius = (7-1)/2 = 3
        int kernelSize = (kernelRadius * 2 + 1);
        samples = float(kernelSize * kernelSize);

        // Use a 7x7 kernel for PCF with the texture array
        for(int x = -kernelRadius; x <= kernelRadius; ++x) {
            for(int y = -kernelRadius; y <= kernelRadius; ++y) {
                shadowFactor += texture(gShadowArrays[shadowInfo.textureHandle], vec4(
                    projCoords.xy + vec2(x, y) * texelSize,
                    float(cascadeIndex),
                    comparisonDepth
                ));
            }
        }
        shadowFactor /= samples;
#else
        shadowFactor = texture(gShadowArrays[shadowInfo.textureHandle], vec4(
            projCoords.xy,
            float(cascadeIndex),
            comparisonDepth
        ));
#endif

    } else {
        texelSize = 1.0 / textureSize(gShadowTextures[shadowInfo.textureHandle], 0);

        float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);

        float distanceScale = 1.0;
        if (shadowInfo.type == LIGHT_TYPE_SPOT) { // Spotlight
            float viewDepth = abs(fragPosLightSpace.z);
            distanceScale = mix(1.0, 3.0, clamp(viewDepth / 50.0, 0.0, 1.0));
        } else if (shadowInfo.type == LIGHT_TYPE_DIRECTIONAL) { // Directional light
            distanceScale = 0.5;
        }

        bias = max(0.005 * (1.0 - cosTheta) * distanceScale, 0.001);
        float comparisonDepth = projCoords.z - bias;

#if USE_PCF
        const int kernelRadius = 1; // 3x3 kernel → radius = (3-1)/2 = 1
        int kernelSize = (kernelRadius * 2 + 1);
        samples = float(kernelSize * kernelSize);

        // Use a 3x3 kernel for PCF
        for(int x = -kernelRadius; x <= kernelRadius; ++x) {
            for(int y = -kernelRadius; y <= kernelRadius; ++y) {
                shadowFactor += texture(gShadowTextures[shadowInfo.textureHandle], vec3(
                    projCoords.xy + vec2(x, y) * texelSize,
                    comparisonDepth
                ));
            }
        }
        shadowFactor /= samples;
#else
        // Single sample - hard shadows
        shadowFactor = texture(gShadowTextures[shadowInfo.textureHandle], vec3(
            projCoords.xy,
            comparisonDepth
        ));
#endif
    }
    
    return clamp(shadowFactor, 0.0, 1.0);
}

float calculateShadow(vec3 fragPosWorld, float fragDepthView, vec3 normal, vec3 lightDir, ShadowBufferData shadowInfo, out int cascadeIndexOut) { // Added fragDepthView parameter
    cascadeIndexOut = -1; // Default value

    if (shadowInfo.type < 0) return 1.0;

    if (isnan(fragDepthView) || isinf(fragDepthView)) {
        return 1.0;
    }

    mat4 lightMatrix;
    int cascadeIndex = 0;

    if (shadowInfo.cascadeCount > 1) {
        cascadeIndex = int(shadowInfo.cascadeCount - 1); // Assume farthest initially
        float blendFactor = 0.0;
        int nextCascadeIndex = -1;

        for (int i = 0; i < int(shadowInfo.cascadeCount - 1); ++i) {
            float cascadeSplitDepth = shadowInfo.cascadeSplitsViewSpace[i].y;

            if (fragDepthView < cascadeSplitDepth) {
                cascadeIndex = i;

                float cascadeStartDepth = shadowInfo.cascadeSplitsViewSpace[i].x;
                float cascadeSize = cascadeSplitDepth - cascadeStartDepth;

                if (cascadeSize > 0.0001) {
                    float blendZoneSize = cascadeSize * CASCADE_BLEND_WIDTH_PERCENT;
                    float blendZoneStart = cascadeSplitDepth - blendZoneSize;

                    if (fragDepthView > blendZoneStart) {
                        blendFactor = (fragDepthView - blendZoneStart) / blendZoneSize;
                        blendFactor = clamp(blendFactor, 0.0, 1.0);
                        nextCascadeIndex = i + 1;
                    }
                }
                break;
            }
        }

        cascadeIndexOut = cascadeIndex;

        if (blendFactor > 0.0 && nextCascadeIndex >= 0 && nextCascadeIndex < int(shadowInfo.cascadeCount)) {
            mat4 lightMatrix1 = shadowInfo.cascadeMatrices[cascadeIndex];
            mat4 lightMatrix2 = shadowInfo.cascadeMatrices[nextCascadeIndex];

            float shadow1 = calculateShadowForCascade(fragPosWorld, normal, lightDir, shadowInfo, lightMatrix1, cascadeIndex);
            float shadow2 = calculateShadowForCascade(fragPosWorld, normal, lightDir, shadowInfo, lightMatrix2, nextCascadeIndex);

            return mix(shadow1, shadow2, blendFactor);
        } else {
            lightMatrix = shadowInfo.cascadeMatrices[cascadeIndex];
            return calculateShadowForCascade(fragPosWorld, normal, lightDir, shadowInfo, lightMatrix, cascadeIndex);
        }

    } else {
        lightMatrix = shadowInfo.cascadeMatrices[0];
        cascadeIndexOut = 0;
        return calculateShadowForCascade(fragPosWorld, normal, lightDir, shadowInfo, lightMatrix, 0);
    }
}

vec3 getIrradiance(vec3 worldPos, vec3 normal, vec3 cameraDirection, ProbeVolume volume) {
    

    vec3 surfaceBias = DDGIGetSurfaceBias(normal, cameraDirection,  volume);
    
    float blendWeight = DDGIGetVolumeBlendWeight(worldPos, volume);

    vec3 irradiance = vec3(0.0);

    if (blendWeight > 0.0) {
        irradiance = DDGIGetVolumeIrradiance(
            worldPos,
            normal,
            surfaceBias,
            gTextureArrays[pc.probeIrradianceHandle],
            gTextureArrays[pc.probeVisibilityHandle],
            gTextureArrays[pc.probeOffsetHandle],
            gUintTextureArrays[pc.probeClassificationHandle],
            volume);

        irradiance *= blendWeight;
    }

    return irradiance;
}

void main() {


    vec4 positionDepth = texture(gTextures[pc.GBufferPositionHandle], fragTexCoord);
    vec3 fragPos = positionDepth.xyz;
    vec3 N = texture(gTextures[pc.GBufferNormalHandle], fragTexCoord).rgb;
    vec4 albedoSpec = texture(gTextures[pc.GBufferAlbedoHandle], fragTexCoord);
    vec4 metallicRoughnessAO = texture(gTextures[pc.GBufferMaterialHandle], fragTexCoord);
    
    N = normalize(N);
    
    vec3 albedo = albedoSpec.rgb;
    float metallic = metallicRoughnessAO.r;
    float roughness = metallicRoughnessAO.g;
    float ao = metallicRoughnessAO.b;
    
    vec3 V = normalize(pc.cameraPos.xyz - fragPos);
    
    vec3 Lo = vec3(0.0);
    int debugCascadeIndex = -1;

    for(uint i = 0; i < pc.lightCount; i++) {
        LightData light = u_lightData[i].lightData;
        vec3 lightPos = light.position.xyz;
        vec3 lightDirWorld;
        float attenuation = 1.0;
        float lightRange = light.direction.w;
        float lightIntensity = light.color.w;
        vec3 lightColor = light.color.rgb;

        float lightType = light.position.w;
        
        if (abs(lightType - 0.0) < 0.1) { // Point light
            lightDirWorld = normalize(lightPos - fragPos); 
            attenuation = calculateAttenuation(lightPos, fragPos, lightRange);
            
        }
        else if (abs(lightType - 1.0) < 0.1) { // Directional light
            lightDirWorld = normalize(-light.direction.xyz); // Negate to get vector towards light source
            attenuation = 1.0;
            
#if DEBUG_DIRECTIONAL_SHADOWS
            // Debug: Show directional light direction as color
            lightColor = abs(light.direction.xyz);
            lightIntensity = 1.0;
#endif
        }
        else if (abs(lightType - 2.0) < 0.1) { // Spot light
            vec3 lightVector = (lightPos - fragPos);

            lightDirWorld = normalize(lightVector); 
            float lightDistance = length(lightVector);
            float  falloff = LightFalloff(lightDistance);
            float  window = LightWindowing(lightDistance, lightRange);
            attenuation = SpotAttenuation(normalize(light.direction.xyz), -lightDirWorld, light.spotAngles.x, light.spotAngles.y);
            attenuation *= falloff * window;
        }
        else {
            continue;
        } 


        float shadowFactor = 1.0;
        int currentCascadeIndex = -1; 
        for (uint j = 0u; j < pc.shadowCount; j++) {
           ShadowBufferData shadowInfo = u_shadowData[j].shadowData;
            if (shadowInfo.lightIndex == light.spotAngles.z && shadowInfo.type >= 0) {
               
               shadowFactor = calculateShadow(fragPos, positionDepth.a, N, lightDirWorld, shadowInfo, currentCascadeIndex);
                if (debugCascadeIndex == -1 && shadowFactor < 1.0) {
                   debugCascadeIndex = currentCascadeIndex;
                }
                break; 
            }
        }


        vec3 contribution = calculateLightContribution(N, V, lightDirWorld, albedo, metallic, roughness, ao, lightColor, lightIntensity);

        Lo += contribution * attenuation * shadowFactor;
    }

    vec3 indirectDiffuse = vec3(0.03) * albedo ;

    if (pc.useDDGI > 0) {
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 kD_indirect = (vec3(1.0) - F0) * (1.0 - metallic);
        
        vec3 irradiance = getIrradiance(fragPos, N, V, u_DDGI_Volume);
        indirectDiffuse = irradiance * (albedo/3.14159265359) * kD_indirect * ao;

        
    }

    vec3 color = indirectDiffuse + Lo;

    // Apply Fog
    if (pc.fogColor.a > 0.5) {
        float fragDepthView = abs(positionDepth.a); // Using view-space depth from G-Buffer
        float fogFactor = smoothstep(pc.fogDistances.x, pc.fogDistances.y, fragDepthView);
        color = mix(color, pc.fogColor.rgb, fogFactor);
    }

#if DEBUG_CASCADES
    // Apply cascade visualization tint if enabled and a cascade was determined
    if (debugCascadeIndex >= 0) {
        vec3 cascadeColorTint = vec3(1.0); // Default: no tint
        if (debugCascadeIndex == 0) cascadeColorTint = vec3(1.0, 0.5, 0.5); // Red tint
        else if (debugCascadeIndex == 1) cascadeColorTint = vec3(0.5, 1.0, 0.5); // Green tint
        else if (debugCascadeIndex == 2) cascadeColorTint = vec3(0.5, 0.5, 1.0); // Blue tint
        else if (debugCascadeIndex == 3) cascadeColorTint = vec3(1.0, 1.0, 0.5); // Yellow tint
        color *= cascadeColorTint;
    }
#endif

    color *= exposure(1.0);
    color = ACESFilm(color);
    color = LinearToSRGB(color);

    outColor = vec4(color, 1.0);
}
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
#define USE_SHADER_BIAS 0  // 0 = rely on hardware depth bias from the shadow pass

#define CASCADE_BLEND_WIDTH_PERCENT 0.15

// Debug: tint each pixel by the DDGI base-probe grid cell it falls into (checkerboard
// per axis), to check whether an irradiance artifact lines up with a probe cell boundary.
#define DEBUG_DDGI_PROBE_GRID 0


layout(set = 3, binding = 0) uniform sampler2D gTextures[];
layout(set = 3, binding = 0) uniform sampler2DShadow gShadowTextures[];
layout(set = 3, binding = 0) uniform sampler2DArrayShadow gShadowArrays[];
layout(set = 3, binding = 0) uniform sampler2DArray gTextureArrays[];
layout(set = 3, binding = 0) uniform usampler2DArray gUintTextureArrays[];
layout(set = 3, binding = 0) uniform samplerCube gCubemaps[];

#include "ddgi/ProbeCommon.glsl"
#include "ddgi/IrradianceCommon.glsl"
#include "common/CameraCommon.glsl"
#include "common/RenderFlags.glsl"
#include "common/ShadingModels.glsl"
#include "common/BRDF.glsl"
#include "common/Tonemapping.glsl"


struct LightData {
    vec4 position;      // w = light type (0 = point, 1 = directional, 2 = spot)
    vec4 direction;     // w = range
    vec4 color;         // w = intensity
    vec4 spotAngles;    // x = inner cone cos, y = outer cone cos, z = entity id, w = unused
};


struct ShadowGPUData {
    int type; // 0 = point, 1 = directional, 2 = spot
    uint cascadeCount;
    uint lightIndex; // Index of the light this shadow maps to
    uint textureHandle;
    mat4 cascadeMatrices[MAX_CASCADES];
    vec4 cascadeSplitsViewSpace[MAX_CASCADES]; // Contains view-space Z split depths in .x component
};

layout(std430, set = 0, binding = 1) readonly buffer LightDataSSBO {
    LightData lights[];
} u_lightSSBO[];


layout(std430, set = 0, binding = 4) readonly buffer ShadowDataSSBO {
    ShadowGPUData shadows[];
} u_shadowSSBO[];


layout(std140, set = 0, binding = 5) uniform ProbeInfo {
    ProbeVolume volume;
} u_probeInfo[];

ProbeVolume u_DDGI_Volume;

vec2 signNotZero(vec2 v) {
    return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec3 octDecodeNormal(vec2 enc) {
    vec3 n = vec3(enc.xy, 1.0 - abs(enc.x) - abs(enc.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * signNotZero(n.xy);
    }
    return normalize(n);
}


// The vec4 leads so the trailing uint block needs no alignment padding, keeping the block under the
// 128 byte guaranteed push constant size
layout(push_constant) uniform PushConstants {
    vec4 cameraPos;

    uint lightDataSSBOIndex;
    uint lightStaticCount;
    uint lightDynamicOffset;
    uint lightDynamicCount;
    uint shadowDataSSBOIndex;
    uint shadowStaticCount;
    uint shadowDynamicOffset;
    uint shadowDynamicCount;

    uint GBufferAlbedoHandle;
    uint GBufferNormalHandle;
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint GBufferMaterialHandle;
    uint GBufferDepthHandle;
    uint GBufferShadingModelHandle;

    uint lightingFlags;
    uint probeVolumeHandle;
    uint probeIrradianceHandle;
    uint probeVisibilityHandle;
    uint probeOffsetHandle;
    uint probeClassificationHandle;
    uint brdfLutHandle;
    uint sssrAccumulatedHandle;
    uint prefilteredEnvHandle;
    float prefilteredEnvMipCount;
} pc;



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



// Helper function to calculate shadow for a specific cascade - this contains the PCF shadow mapping logic
float calculateShadowForCascade(vec3 fragPosWorld, vec3 normal, vec3 lightDir, ShadowGPUData shadowInfo, 
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
    float samples;

    if (shadowInfo.cascadeCount > 1) {

        texelSize = 1.0 / vec2(textureSize(gShadowArrays[shadowInfo.textureHandle], 0));

#if USE_SHADER_BIAS
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

        float bias = max(0.005 * (1.0 - cosTheta) * distanceScale * cascadeBiasMultiplier, 0.0005);
        float comparisonDepth = projCoords.z - bias;
#else
        float comparisonDepth = projCoords.z;
#endif

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

#if USE_SHADER_BIAS
        float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);

        float distanceScale = 1.0;
        if (shadowInfo.type == LIGHT_TYPE_SPOT) { // Spotlight
            float viewDepth = abs(fragPosLightSpace.z);
            distanceScale = mix(1.0, 3.0, clamp(viewDepth / 50.0, 0.0, 1.0));
        } else if (shadowInfo.type == LIGHT_TYPE_DIRECTIONAL) { // Directional light
            distanceScale = 0.5;
        }

        float bias = max(0.005 * (1.0 - cosTheta) * distanceScale, 0.001);
        float comparisonDepth = projCoords.z - bias;
#else
        float comparisonDepth = projCoords.z;
#endif

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

float calculateShadow(vec3 fragPosWorld, float fragDepthView, vec3 normal, vec3 lightDir, ShadowGPUData shadowInfo, out int cascadeIndexOut) { // Added fragDepthView parameter
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

vec3 getSpecularDominantDir(vec3 N, vec3 R, float roughness) {
    float smoothness = clamp(1.0 - roughness, 0.0, 1.0);
    float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
    return mix(N, R, lerpFactor);
}

vec3 getIrradiance(vec3 worldPos, vec3 surfaceNormal, vec3 sampleDirection, vec3 cameraDirection, ProbeVolume volume) {


    vec3 surfaceBias = DDGIGetSurfaceBias(surfaceNormal, cameraDirection,  volume);

    float blendWeight = DDGIGetVolumeBlendWeight(worldPos, volume);

    vec3 irradiance = vec3(0.0);

    if (blendWeight > 0.0) {
        irradiance = DDGIGetVolumeIrradiance(
            worldPos,
            sampleDirection,
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
    u_DDGI_Volume = u_probeInfo[0].volume;

    float depth = texture(gTextures[pc.GBufferDepthHandle], fragTexCoord).r;

    // Background pixel (no geometry written) - nothing to light
    if (depth >= 1.0) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];

    // Reconstruct world position from depth + screen UV using the inverse view-projection
    vec3 ndc = vec3(fragTexCoord * 2.0 - 1.0, depth);
    vec4 worldH = cam.invViewProj * vec4(ndc, 1.0);
    vec3 fragPos = worldH.xyz / worldH.w;

    // View-space linear depth (positive into screen), matches the old gPositionDepth.a
    float viewDepth = -(cam.view * vec4(fragPos, 1.0)).z;

    vec4 normalMotion = texture(gTextures[pc.GBufferNormalHandle], fragTexCoord);

    vec3 N = octDecodeNormal(normalMotion.rg);
    if ((pc.lightingFlags & RENDER_SHOW_NORMALS) != 0u) {
        outColor = vec4(N, 1.0);
        return;
    }

    if ((pc.lightingFlags & RENDER_SHOW_MOTION) != 0u) {
        // Pixels per frame, saturating at 16. Red is rightward, green downward, flat grey is static.
        // The composite pass still applies exposure and ACES to this, which flatten anything near
        // 1.0 into a narrow band, so the range is centred low where the curve still has slope
        vec2 motionPixels = normalMotion.zw * vec2(textureSize(gTextures[pc.GBufferNormalHandle], 0));
        vec2 encoded = clamp(motionPixels / 16.0, -1.0, 1.0) * 0.1 + 0.1;
        outColor = vec4(encoded, 0.1, 1.0);
        return;
    }

    vec4 baseColorEmissive = texture(gTextures[pc.GBufferAlbedoHandle], fragTexCoord);
    vec4 material = texture(gTextures[pc.GBufferMaterialHandle], fragTexCoord);
    vec4 shadingModel = texture(gTextures[pc.GBufferShadingModelHandle], fragTexCoord);

    vec3 albedo = baseColorEmissive.rgb;
    vec3 emissive = albedo * unpackEmissive(baseColorEmissive.a);
    float metallic = material.r;
    float roughness = material.g;
    float ao = material.b;
    float specular = unpackSpecular(material.a);
    uint shadingModelId = unpackShadingModel(shadingModel.r);

    vec3 V = normalize(pc.cameraPos.xyz - fragPos);

#if DEBUG_DDGI_PROBE_GRID
    {
        vec2 debugOctUV = DDGIGetOctahedralCoordinates(N);
        outColor = vec4(debugOctUV * 0.5 + 0.5, 0.0, 1.0);
        return;
    }
#endif

    vec3 Lo = vec3(0.0);
    int debugCascadeIndex = -1;

    uint lightCount = ((pc.lightingFlags & RENDER_SHOW_DIRECT) != 0u) ? (pc.lightStaticCount + pc.lightDynamicCount) : 0u;
    for(uint li = 0; li < lightCount; li++) {
        uint lightSlot = (li < pc.lightStaticCount) ? li : (pc.lightDynamicOffset + li - pc.lightStaticCount);
        LightData light = u_lightSSBO[pc.lightDataSSBOIndex].lights[lightSlot];
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
        uint shadowCount = pc.shadowStaticCount + pc.shadowDynamicCount;
        for (uint sj = 0u; sj < shadowCount; sj++) {
            uint shadowSlot = (sj < pc.shadowStaticCount) ? sj : (pc.shadowDynamicOffset + sj - pc.shadowStaticCount);
            ShadowGPUData shadowInfo = u_shadowSSBO[pc.shadowDataSSBOIndex].shadows[shadowSlot];
            if (shadowInfo.lightIndex == light.spotAngles.z && shadowInfo.type >= 0) {
               
               shadowFactor = calculateShadow(fragPos, viewDepth, N, lightDirWorld, shadowInfo, currentCascadeIndex);
                if (debugCascadeIndex == -1) {
                   debugCascadeIndex = currentCascadeIndex;
                }
                break; 
            }
        }


        vec3 brdf = evalBRDF(shadingModelId, N, V, lightDirWorld, albedo, metallic, roughness, specular);

        Lo += brdf * lightColor * lightIntensity * attenuation * shadowFactor;
    }

    vec3 indirectDiffuse = vec3(0.0);
    vec3 indirectSpecular = vec3(0.0);

    if ((pc.lightingFlags & RENDER_SHOW_INDIRECT) != 0u) {
        if ((pc.lightingFlags & RENDER_USE_GLOBAL_ILLUMINATION) != 0u) {
            vec3 irradiance = getIrradiance(fragPos, N, N, V, u_DDGI_Volume);

            if ((pc.lightingFlags & RENDER_MODULATE_INDIRECT) != 0u) {
                vec3 F0 = computeF0(albedo, metallic, specular);
                vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

                // Karis split-sum environment BRDF: the integral of the full GGX lobe, so it carries
                // the geometry term the Fresnel-only approximation above leaves out
                vec2 envBRDF = texture(gTextures[nonuniformEXT(pc.brdfLutHandle)], vec2(max(dot(N, V), 0.0), roughness)).rg;
                vec3 specularWeight = F0 * envBRDF.x + envBRDF.y;

                vec3 kD_indirect = (vec3(1.0) - F) * (1.0 - metallic);
                indirectDiffuse = irradiance * (albedo / PI) * kD_indirect * ao;

                // The other half of the split sum: incident radiance already integrated against the
                // GGX lobe, one mip per roughness. The prefilter bakes mip i at roughness
                // i / (mips - 1), so the lookup is linear in roughness to match it.
                vec3 R = reflect(-V, N);
                vec3 Rd = getSpecularDominantDir(N, R, roughness);
                vec3 prefilteredRadiance =
                    textureLod(gCubemaps[nonuniformEXT(pc.prefilteredEnvHandle)], Rd,
                               roughness * (pc.prefilteredEnvMipCount - 1.0)).rgb;

                indirectSpecular = prefilteredRadiance * specularWeight * ao;
            } else {
                indirectDiffuse = irradiance;
            }
        } else {
            indirectDiffuse = vec3(0.03) * albedo;
        }
    }

    vec3 color = indirectDiffuse + indirectSpecular + Lo + emissive;

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

    outColor = vec4(color, 1.0);
}

#version 450
#extension GL_EXT_nonuniform_qualifier : require

#ifndef PROBE_OFFSETS_TEXTURE
#define PROBE_OFFSETS_TEXTURE
#endif


#ifndef DDGI_ENABLE_PROBE_CLASSIFICATION
#define DDGI_ENABLE_PROBE_CLASSIFICATION
#endif

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 fragTexCoord;


// Define max light count - must match the C++ side
#define MAX_LIGHTS 16

// Light types
#define LIGHT_TYPE_POINT       0
#define LIGHT_TYPE_DIRECTIONAL 1
#define LIGHT_TYPE_SPOT        2

// Add a debug mode flag at the top
#define DEBUG_SPOTLIGHTS 0
#define MAX_CASCADES 4
#define MAX_SHADOW_CASTERS 4
#define DEBUG_CASCADES 0
#define DEBUG_DIRECTIONAL_SHADOWS 0  // Set to 1 to enable debugging
#define DEBUG_SHADOW_COORDS 0

// Define the relative width of the blend zone at the end of each cascade
#define CASCADE_BLEND_WIDTH_PERCENT 0.15 // 10% blend width


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


// Simple Fresnel approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Simple distribution function
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

// Geometry term - Smith's method with GGX
float geometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
    
    return ggx1 * ggx2;
}


// Calculate attenuation for point/spot lights
float calculateAttenuation(vec3 lightPos, vec3 fragPos, float range) {
    float distance = length(lightPos - fragPos);
    float attenuation = 1.0;
    
    // Apply attenuation only if it's a point or spot light with specified range
    if (range > 0.0) {
        // Quadratic attenuation with range control
        float rangeSquared = range * range;
        attenuation = clamp(1.0 - (distance * distance) / rangeSquared, 0.0, 1.0);
        attenuation *= attenuation; // Apply squared falloff for smoother transition
    }
    
    return attenuation;
}

// Calculate spot light cone effect
float calculateSpotEffect(vec3 lightToFrag, vec3 spotDirection, float cosInnerAngle, float cosOuterAngle) {
    // lightToFrag: vector from light to fragment (points toward the fragment)
    // spotDirection: direction the spotlight is pointing (points away from the light)
    
    // Calculate the cosine of the angle between the negative light-to-fragment direction 
    // and the spotlight direction.
    // dot(-lightToFragDir, spotDirection) gives cos(angle between them)
    float cosAngle = dot(-lightToFrag, spotDirection); 
    
#if DEBUG_SPOTLIGHTS
    // For debugging, return a clear visualization
    // 0.0 = outside cone (black)
    // 0.25 = between outer and inner (red)
    // 1.0 = inside inner cone (green)
    if (cosAngle < cosOuterAngle)
        return 0.0; // Outside cone
    else if (cosAngle < cosInnerAngle)
        return 0.25; // Between outer and inner cone
    else
        return 1.0; // Inside inner cone
#else
    // Normal mode
    // Return 0 if outside outer cone
    if (cosAngle < cosOuterAngle) return 0.0;
    
    // Return 1 if inside inner cone
    if (cosAngle > cosInnerAngle) return 1.0;
    
    // Smooth interpolation between outer and inner cone
    // Use smoothstep for a nicer gradient
    return smoothstep(cosOuterAngle, cosInnerAngle, cosAngle);
#endif
}

// Helper function to calculate light contribution for PBR
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
    if (shadowInfo.type == 1) { // Directional light
        // Show coordinates as colors
        vec3 debugColor = vec3(projCoords.xy, projCoords.z * 0.1);
        return length(debugColor); // Return a debug value
    }
#endif
    
    // Check if fragment is outside the light's view frustum [0, 1] range
    if(projCoords.x < 0.0 || projCoords.x > 1.0 || 
       projCoords.y < 0.0 || projCoords.y > 1.0 ||
       projCoords.z < 0.0 || projCoords.z > 1.0) { // Check Z too
        
#if DEBUG_DIRECTIONAL_SHADOWS
        // Debug: Show fragments outside shadow frustum as red for directional lights
        if (shadowInfo.type == 1) {
            return 0.0; // Make them fully shadowed to see the issue
        }
#endif
        return 1.0; // Outside frustum = Not shadowed (fully lit)
    }
    
    // Create the appropriate sampler based on cascade count
    float shadowFactor = 0.0;
    vec2 texelSize;
    float bias;
    float samples;

    if (shadowInfo.cascadeCount > 1) {

        // Use texture array for cascaded shadow mapping
        texelSize = 1.0 / vec2(textureSize(gShadowArrays[shadowInfo.textureHandle], 0));
        
        // Apply bias to avoid shadow acne - adjust based on surface angle and cascade level
        float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);
        // Progressively reduce bias for farther cascades to reduce light leaking
        float cascadeBiasMultiplier = 1.0 / (1.0 + float(cascadeIndex) * 0.5);
        
        // Use an adaptive bias that scales with distance (for spotlights)
        float distanceScale = 1.0;
        if (shadowInfo.type == 2) { // Spotlight
            // Increase bias with distance to handle perspective distortion
            float viewDepth = abs(fragPosLightSpace.z);
            distanceScale = mix(1.0, 3.0, clamp(viewDepth / 50.0, 0.0, 1.0));
        }else if (shadowInfo.type == 1) { // Directional light
            // Directional lights need less bias since they use orthographic projection
            distanceScale = 0.5;
        }
        
        
        bias = max(0.005 * (1.0 - cosTheta) * distanceScale * cascadeBiasMultiplier, 0.0005);
        float comparisonDepth = projCoords.z - bias;

        const int kernelRadius = 3; // 7x7 kernel → radius = (7-1)/2 = 3
        int kernelSize = (kernelRadius * 2 + 1);
        samples = float(kernelSize * kernelSize);

        // Use a 3x3 kernel for PCF with the texture array
        for(int x = -kernelRadius; x <= kernelRadius; ++x) {
            for(int y = -kernelRadius; y <= kernelRadius; ++y) {        
                // Use vec4 for sampler2DArrayShadow: vec4(u, v, layer, comparisonValue)
                shadowFactor += texture(gShadowArrays[shadowInfo.textureHandle], vec4(
                    projCoords.xy + vec2(x, y) * texelSize,
                    float(cascadeIndex),
                    comparisonDepth
                ));
            }
        }

    } else {
        // Use bindless shadow map
        texelSize = 1.0 / textureSize(gShadowTextures[shadowInfo.textureHandle], 0);

        // Apply bias to avoid shadow acne - adjust based on surface angle
        float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);
        
        // Use different bias strategies for different light types
        float distanceScale = 1.0;
        if (shadowInfo.type == 2) { // Spotlight
            // Increase bias with distance to handle perspective distortion
            float viewDepth = abs(fragPosLightSpace.z);
            distanceScale = mix(1.0, 3.0, clamp(viewDepth / 50.0, 0.0, 1.0));
        } else if (shadowInfo.type == 1) { // Directional light
            // Directional lights need less bias since they use orthographic projection
            distanceScale = 0.5;
        }
        
        bias = max(0.005 * (1.0 - cosTheta) * distanceScale, 0.001);
        float comparisonDepth = projCoords.z - bias; 

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
    }
    
    shadowFactor /= samples; // Average the results
    
    return clamp(shadowFactor, 0.0, 1.0);
}

// Modified to use linear view-space depth from G-buffer
float calculateShadow(vec3 fragPosWorld, float fragDepthView, vec3 normal, vec3 lightDir, ShadowBufferData shadowInfo, out int cascadeIndexOut) { // Added fragDepthView parameter
    cascadeIndexOut = -1; // Default value

    if (shadowInfo.type < 0) return 1.0; // No shadow for this light or unsupported type


    mat4 lightMatrix;
    int cascadeIndex = 0;

    // Check if we're using cascaded shadow mapping
    if (shadowInfo.cascadeCount > 1) {
        // Select cascade based on depth and calculate blend factor
        cascadeIndex = int(shadowInfo.cascadeCount - 1); // Assume farthest initially
        float blendFactor = 0.0;
        int nextCascadeIndex = -1;

        // Loop through the split planes (boundary between cascade i and i+1)
        for (int i = 0; i < int(shadowInfo.cascadeCount - 1); ++i) {
            // Split depth marks the FAR plane of cascade 'i' in view space Z
            // Assuming cascadeSplitsViewSpace.x holds positive linear view Z depth
            float cascadeSplitDepth = shadowInfo.cascadeSplitsViewSpace[i].y;

            // If fragment depth is less than this split depth, it belongs to cascade 'i' or earlier
            if (fragDepthView < cascadeSplitDepth) {
                cascadeIndex = i;

                // Calculate the start depth (NEAR plane) of this cascade in view space Z
                // Assuming positive depths, near plane of first cascade is technically 0? Or camera near plane?
                // Using 0.0 might be problematic if near plane > 0. Check C++ split calculation.
                float cascadeStartDepth = (i == 0) ? 0.0 : shadowInfo.cascadeSplitsViewSpace[i-1].y;

                // Calculate the size of this cascade's depth range
                float cascadeSize = cascadeSplitDepth - cascadeStartDepth;

                // Avoid division by zero or negative size if splits are invalid
                if (cascadeSize > 0.0001) {
                    // Calculate the absolute size of the blend zone at the end of this cascade
                    float blendZoneSize = cascadeSize * CASCADE_BLEND_WIDTH_PERCENT;

                    // Calculate the start of the blend zone (depth value where blending begins)
                    float blendZoneStart = cascadeSplitDepth - blendZoneSize;

                    // Check if fragment depth is within the blend zone [blendZoneStart, cascadeSplitDepth]
                    if (fragDepthView > blendZoneStart) {
                        // Calculate blend factor: 0 at blendZoneStart, 1 at cascadeSplitDepth
                        blendFactor = (fragDepthView - blendZoneStart) / blendZoneSize;
                        blendFactor = clamp(blendFactor, 0.0, 1.0); // Ensure it's within [0, 1]
                        nextCascadeIndex = i + 1;
                    }
                }

                // Found the primary cascade (and potential blend zone), no need to check further splits
                break;
            }
        }

        cascadeIndexOut = cascadeIndex; // Output the primary cascade index

        // Perform shadow calculation(s) based on whether blending is needed
        if (blendFactor > 0.0 && nextCascadeIndex >= 0 && nextCascadeIndex < int(shadowInfo.cascadeCount)) {
            // Blend between cascadeIndex and nextCascadeIndex
            mat4 lightMatrix1 = shadowInfo.cascadeMatrices[cascadeIndex];
            mat4 lightMatrix2 = shadowInfo.cascadeMatrices[nextCascadeIndex];

            float shadow1 = calculateShadowForCascade(fragPosWorld, normal, lightDir, shadowInfo, lightMatrix1, cascadeIndex);
            float shadow2 = calculateShadowForCascade(fragPosWorld, normal, lightDir, shadowInfo, lightMatrix2, nextCascadeIndex);

            // Linearly interpolate between the two shadow values
            return mix(shadow1, shadow2, blendFactor);
        } else {
            // No blending needed, use only the selected cascadeIndex
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
        // Get irradiance for the world-space position in the volume
        irradiance = DDGIGetVolumeIrradiance(
            worldPos,
            normal,
            surfaceBias,
            gTextureArrays[pc.probeIrradianceHandle],
            gTextureArrays[pc.probeVisibilityHandle],
            gTextureArrays[pc.probeOffsetHandle],
#ifdef DDGI_ENABLE_PROBE_CLASSIFICATION
            gUintTextureArrays[pc.probeClassificationHandle],
#endif
            volume);

        irradiance *= blendWeight;
    }

    return irradiance;
}

void main() {


    // Sample from GBuffer textures
    vec4 positionDepth = texture(gTextures[pc.GBufferPositionHandle], fragTexCoord);
    vec3 fragPos = positionDepth.xyz;
    vec3 N = texture(gTextures[pc.GBufferNormalHandle], fragTexCoord).rgb;
    vec4 albedoSpec = texture(gTextures[pc.GBufferAlbedoHandle], fragTexCoord);
    vec4 metallicRoughnessAO = texture(gTextures[pc.GBufferMaterialHandle], fragTexCoord);
    
    N = normalize(N);
    
    // Extract material properties from GBuffer
    vec3 albedo = albedoSpec.rgb;
    float metallic = metallicRoughnessAO.r;
    float roughness = metallicRoughnessAO.g;
    float ao = metallicRoughnessAO.b;
    
    vec3 V = normalize(pc.cameraPos.xyz - fragPos);
    
    vec3 Lo = vec3(0.0);
    int debugCascadeIndex = -1; // Store the cascade index for debugging

    // Process all active lights
    for(uint i = 0; i < pc.lightCount; i++) {
        LightData light = u_lightData[i].lightData;
        vec3 lightPos = light.position.xyz;
        vec3 lightDirWorld;
        float attenuation = 1.0;
        float lightRange = light.direction.w;
        float lightIntensity = light.color.w;
        vec3 lightColor = light.color.rgb;

        // Handle different light types
        float lightType = light.position.w;
        
        if (abs(lightType - 0.0) < 0.1) { // Point light
            lightDirWorld = normalize(lightPos - fragPos); 
            attenuation = calculateAttenuation(lightPos, fragPos, lightRange);
            
        }
        else if (abs(lightType - 1.0) < 0.1) { // Directional light
           // Directional light: direction is constant, coming FROM the specified direction
            lightDirWorld = normalize(-light.direction.xyz); // Negate to get vector towards light source
            attenuation = 1.0; // No distance attenuation
            
#if DEBUG_DIRECTIONAL_SHADOWS
            // Debug: Show directional light direction as color
            lightColor = abs(light.direction.xyz);
            lightIntensity = 1.0;
#endif
        }
        else if (abs(lightType - 2.0) < 0.1) { // Spot light
            lightDirWorld = normalize(lightPos - fragPos); 
            attenuation = calculateAttenuation(lightPos, fragPos, lightRange);

            // Apply spot light cone effect
            attenuation *= calculateSpotEffect(
                lightDirWorld,            
                normalize(light.direction.xyz), 
                light.spotAngles.x, 
                light.spotAngles.y
            );
            
        }
        else {
            // Unknown light type, skip
            continue;
        }

#if DEBUG_SPOTLIGHTS
        if (abs(lightType - 2.0) < 0.1 && attenuation > 0.0) { 
           float spotEffectValue = calculateSpotEffect(lightDirWorld, normalize(light.direction.xyz), light.spotAngles.x, light.spotAngles.y);
           if (spotEffectValue < 0.01) { // Outside
               lightColor = vec3(1.0, 0.0, 1.0); // Purple
           } else if (spotEffectValue < 0.99) { // Penumbra
                lightColor = vec3(1.0, 0.0, 0.0); // Red
           } else { // Umbra
                lightColor = vec3(0.0, 1.0, 0.0); // Green
           }
           attenuation = 1.0; 
        }
#endif   


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
        // Calculate F0 (surface reflection at zero incidence)
        vec3 F0 = mix(vec3(0.04), albedo, metallic);

        // Specular BRDF terms
        float NdotV = max(dot(N, V), 0.0001);

        //vec3 kD_indirect = vec3(1.0) * (1.0 - metallic);
        vec3 kD_indirect = (vec3(1.0) - fresnelSchlick(NdotV, F0)) * (1.0 - metallic);
        
        vec3 indirectDiffuesIntensity = getIrradiance(fragPos, N, V, u_DDGI_Volume);
        indirectDiffuse = indirectDiffuesIntensity; //* (albedo/3.14159265359) * kD_indirect;

        
    }

    vec3 color = indirectDiffuse;

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

    // HDR tonemapping and gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2)); 

    outColor = vec4(color, 1.0);
}
#version 450
#extension GL_EXT_nonuniform_qualifier : require

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

// Define the relative width of the blend zone at the end of each cascade
#define CASCADE_BLEND_WIDTH_PERCENT 0.15 // 10% blend width


layout(set = 0, binding = 0) uniform sampler2D gPositionDepth;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gAlbedoSpec;
layout(set = 0, binding = 3) uniform sampler2D gMetallicRoughnessAO;

layout(set = 3, binding = 0) uniform sampler2DShadow gBindlessTextures[];


// Light data structure for shader
struct LightData {
    vec4 position;      // w = light type (0 = point, 1 = directional, 2 = spot)
    vec4 direction;     // w = range
    vec4 color;         // w = intensity
    vec4 spotAngles;    // x = inner cone cos, y = outer cone cos, z = unused, w = unused
};



struct ShadowBufferData {
    int type; // 0 = point, 1 = directional, 2 = spot
    uint cascadeCount;
    uint lightIndex; // Index of the light this shadow maps to
    uint textureHandle;
    mat4 cascadeMatrices[MAX_CASCADES];
    vec4 cascadeSplitsViewSpace[MAX_CASCADES]; // Contains view-space Z split depths in .x component
};

layout(std140, set = 0, binding = 4) uniform LightUniformBufferObject {
    uint numLights;
    LightData lights[MAX_LIGHTS];
} u_lights;


layout(std140, set = 0, binding = 5) uniform ShadowDataLayout {
    uint shadowCount;
    ShadowBufferData shadowData[MAX_LIGHTS];
} u_shadowData;



// Push constant for per-object data
layout(push_constant) uniform PushConstants {
    vec3 camPos;
} pushConstants;

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
    
    // Check if fragment is outside the light's view frustum [0, 1] range
    if(projCoords.x < 0.0 || projCoords.x > 1.0 || 
       projCoords.y < 0.0 || projCoords.y > 1.0 ||
       projCoords.z < 0.0 || projCoords.z > 1.0) { // Check Z too
        return 1.0; // Outside frustum = Not shadowed (fully lit)
    }
    
    // Create the appropriate sampler based on cascade count
    float shadowFactor = 0.0;
    vec2 texelSize;
    float bias;

    if (shadowInfo.cascadeCount > 1) {

    } else {
        // Use bindless shadow map
        texelSize = 1.0 / textureSize(gBindlessTextures[shadowInfo.textureHandle], 0);

        // Apply bias to avoid shadow acne - adjust based on surface angle
        float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);
        
        // Use an adaptive bias that scales with distance (for spotlights)
        float distanceScale = 1.0;
        if (shadowInfo.type == 2) { // Spotlight
            // Increase bias with distance to handle perspective distortion
            float viewDepth = abs(fragPosLightSpace.z);
            distanceScale = mix(1.0, 3.0, clamp(viewDepth / 50.0, 0.0, 1.0));
        }
        
        bias = max(0.005 * (1.0 - cosTheta) * distanceScale, 0.001);
        
        // Use a 3x3 kernel for PCF
        for(int x = -1; x <= 1; ++x) {
            for(int y = -1; y <= 1; ++y) {
                float comparisonDepth = projCoords.z - bias; 
                shadowFactor += texture(gBindlessTextures[shadowInfo.textureHandle], vec3(
                    projCoords.xy + vec2(x, y) * texelSize,
                    comparisonDepth
                ));
            }
        }
    }
    
    shadowFactor /= 9.0; // Average the results
    
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
        return 1.0;
    } else {
        lightMatrix = shadowInfo.cascadeMatrices[0];
        cascadeIndexOut = 0;
        return calculateShadowForCascade(fragPosWorld, normal, lightDir, shadowInfo, lightMatrix, 0);
    }
}

void main() {


    // Sample from GBuffer textures
    vec4 positionDepth = texture(gPositionDepth, fragTexCoord);
    vec3 fragPos = positionDepth.xyz;
    vec3 N = texture(gNormal, fragTexCoord).rgb;
    vec4 albedoSpec = texture(gAlbedoSpec, fragTexCoord);
    vec4 metallicRoughnessAO = texture(gMetallicRoughnessAO, fragTexCoord);
    
    N = normalize(N);
    
    // Extract material properties from GBuffer
    vec3 albedo = albedoSpec.rgb;
    float metallic = metallicRoughnessAO.r;
    float roughness = metallicRoughnessAO.g;
    float ao = metallicRoughnessAO.b;
    
    vec3 V = normalize(pushConstants.camPos - fragPos);
    
    vec3 Lo = vec3(0.0);
    int debugCascadeIndex = -1; // Store the cascade index for debugging

    // Process all active lights
    for(uint i = 0; i < u_lights.numLights; i++) {
        LightData light = u_lights.lights[i];
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
        for (uint j = 0u; j < u_shadowData.shadowCount; j++) {
           ShadowBufferData shadowInfo = u_shadowData.shadowData[j];
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

    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color = ambient + Lo;

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
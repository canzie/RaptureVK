#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;

#define MAX_LIGHTS 16

layout(set = 1, binding = 0) uniform Material {
    vec3 albedo;
    float roughness;
    float metallic;
} material;

layout(set = 1, binding = 1) uniform sampler2D u_albedoMap;
layout(set = 1, binding = 2) uniform sampler2D u_normalMap;
layout(set = 1, binding = 3) uniform sampler2D u_metallicRoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D u_aoMap;

// Light data structure for shader
struct LightData {
    vec4 position;      // w = light type (0 = point, 1 = directional, 2 = spot)
    vec4 direction;     // w = range
    vec4 color;         // w = intensity
    vec4 spotAngles;    // x = inner cone cos, y = outer cone cos, z = unused, w = unused
};

layout(std140, set = 0, binding = 1) uniform LightUniformBufferObject {
    uint numLights;
    LightData lights[MAX_LIGHTS];
} u_lights;



// Push constant for per-object data
layout(push_constant) uniform PushConstants {
    mat4 model;
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

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}

// Function to calculate tangent space normal from normal map using pre-computed TBN
vec3 getNormalFromMap()
{
    vec3 sampledTexNormal = texture(u_normalMap, fragTexCoord).xyz;
    sampledTexNormal.g = 1.0 - sampledTexNormal.g; // Invert G channel
    vec3 tangentNormal = sampledTexNormal * 2.0 - 1.0;
    
    // Interpolated vectors from vertex shader
    vec3 N_interp = normalize(fragNormal);
    vec3 T_interp = normalize(fragTangent);
    vec3 B_interp = normalize(fragBitangent); // Contains original handedness from aTangent.w

    // Re-orthogonalize TBN vectors
    // N_interp is considered the primary direction
    vec3 N_final = N_interp;

    // Make T_final orthogonal to N_final, based on T_interp
    vec3 T_final = normalize(T_interp - dot(T_interp, N_final) * N_final);

    // Compute B_final from N_final and T_final.
    // Adjust its sign based on the original B_interp to preserve handedness.
    vec3 B_candidate = cross(N_final, T_final);
    if (dot(B_candidate, B_interp) < 0.0) {
        B_candidate = -B_candidate;
    }
    vec3 B_final = normalize(B_candidate); // Ensure unit length
    
    // Form TBN matrix from re-orthogonalized vectors
    mat3 TBN = mat3(T_final, B_final, N_final);

    return normalize(TBN * tangentNormal);
}

// Helper function for point light attenuation
float calculatePointLightAttenuation(vec3 lightPos, float range, vec3 fragPos) {
    float distance = length(lightPos - fragPos);
    float attenuation = 1.0 / (distance * distance);
    
    // Smooth falloff at range boundary
    float rangeAttenuation = 1.0 - smoothstep(range * 0.75, range, distance);
    return attenuation * rangeAttenuation;
}

// Helper function for spot light calculations
float calculateSpotLightAttenuation(vec3 L, vec3 spotDir, float innerCos, float outerCos) {
    float theta = dot(-L, normalize(spotDir));
    float epsilon = innerCos - outerCos;
    return clamp((theta - outerCos) / epsilon, 0.0, 1.0);
}

// Helper function to calculate light contribution for PBR
vec3 calculateLightContribution(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, float ao, vec3 lightColor, float intensity) {
    vec3 H = normalize(V + L);
    
    // Calculate F0 (surface reflection at zero incidence)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    float NDF = distributionGGX(N, H, roughness);       
    float G = GeometrySmith(N, V, L, roughness);  

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;  

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    float NdotL = max(dot(N, L), 0.0);        
    return (kD * albedo / 3.14159265359 + specular) * lightColor * intensity * NdotL;
}

void main() {
    // Sample textures
    vec3 albedoTexture = texture(u_albedoMap, fragTexCoord).rgb;
    vec3 metallicRoughness = texture(u_metallicRoughnessMap, fragTexCoord).rgb;
    float ao = 1.0;
    
    // Extract metallic and roughness from texture (assuming standard format)
    float metallic = metallicRoughness.b;
    float roughness = max(metallicRoughness.g, 0.04);
    
    vec3 finalAlbedo = material.albedo * albedoTexture;
    vec3 N = getNormalFromMap();
    vec3 V = normalize(pushConstants.camPos - fragPos);
    
    vec3 Lo = vec3(0.0);
    
    // Process all active lights
    for(uint i = 0; i < u_lights.numLights; i++) {
        LightData light = u_lights.lights[i];
        vec3 lightPos = light.position.xyz;
        vec3 L;
        float attenuation = 1.0;
        
        // Handle different light types
        float lightType = light.position.w;
        
        if (abs(lightType - 0.0) < 0.1) { // Point light
            L = normalize(lightPos - fragPos);
            attenuation = calculatePointLightAttenuation(lightPos, light.direction.w, fragPos);
        }
        else if (abs(lightType - 2.0) < 0.1) { // Spot light
            L = normalize(lightPos - fragPos);
            float baseAttenuation = calculatePointLightAttenuation(lightPos, light.direction.w, fragPos);
            float spotAttenuation = calculateSpotLightAttenuation(L, light.direction.xyz, light.spotAngles.x, light.spotAngles.y);
            attenuation = baseAttenuation * spotAttenuation;
        }
        else if (abs(lightType - 1.0) < 0.1) { // Directional light (skip for now)
            continue;
        }
        else {
            // Unknown light type, skip
            continue;
        }
        
        vec3 contribution = calculateLightContribution(N, V, L, finalAlbedo, metallic, roughness, ao, light.color.rgb, light.color.w);
        Lo += contribution * attenuation;
    }

    vec3 ambient = vec3(0.03) * finalAlbedo * ao;
    vec3 color = ambient + Lo;

    // HDR tonemapping and gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2)); 

    outColor = vec4(color, 1.0);
}
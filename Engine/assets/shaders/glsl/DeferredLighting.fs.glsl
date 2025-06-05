#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 fragTexCoord;


#define MAX_LIGHTS 16

layout(set = 0, binding = 0) uniform sampler2D gPositionDepth;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gAlbedoSpec;
layout(set = 0, binding = 3) uniform sampler2D gMetallicRoughnessAO;


// Light data structure for shader
struct LightData {
    vec4 position;      // w = light type (0 = point, 1 = directional, 2 = spot)
    vec4 direction;     // w = range
    vec4 color;         // w = intensity
    vec4 spotAngles;    // x = inner cone cos, y = outer cone cos, z = unused, w = unused
};

layout(std140, set = 0, binding = 4) uniform LightUniformBufferObject {
    uint numLights;
    LightData lights[MAX_LIGHTS];
} u_lights;



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


    // Sample from GBuffer textures
    vec4 positionDepth = texture(gPositionDepth, fragTexCoord);
    vec3 fragPos = positionDepth.xyz;
    vec3 N = texture(gNormal, fragTexCoord).rgb;
    vec4 albedoSpec = texture(gAlbedoSpec, fragTexCoord);
    vec4 metallicRoughnessAO = texture(gMetallicRoughnessAO, fragTexCoord);
    
    // Extract material properties from GBuffer
    vec3 albedo = albedoSpec.rgb;
    float metallic = metallicRoughnessAO.r;
    float roughness = metallicRoughnessAO.g;
    float ao = metallicRoughnessAO.b;
    
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
        else if (abs(lightType - 1.0) < 0.1) { // Directional light
            L = normalize(-light.direction.xyz);
            attenuation = 1.0;
        }
        else {
            // Unknown light type, skip
            continue;
        }
        
        vec3 contribution = calculateLightContribution(N, V, L, albedo, metallic, roughness, ao, light.color.rgb, light.color.w);
        Lo += contribution * attenuation;
    }

    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color = ambient + Lo;

    // HDR tonemapping and gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2)); 

    outColor = vec4(color, 1.0);
}
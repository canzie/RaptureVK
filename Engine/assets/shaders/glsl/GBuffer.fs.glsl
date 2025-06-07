#version 460 core

layout(location = 0) out vec4 gPositionDepth; // Renamed and changed to vec4
layout(location = 1) out vec4 gNormal; // Changed from vec3 to vec4
layout(location = 2) out vec4 gAlbedoSpec;
layout(location = 3) out vec4 gMaterial; // R: Metallic, G: Roughness, B: AO - Changed to vec4

layout(location = 0) in vec4 inFragPosDepth;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

precision highp float;


// PBR textures
layout(set = 1, binding = 1) uniform sampler2D u_albedoMap;
layout(set = 1, binding = 2) uniform sampler2D u_normalMap;
layout(set = 1, binding = 3) uniform sampler2D u_metallicRoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D u_aoMap;


layout(set = 1, binding = 0) uniform Material {
    vec3 albedo;
    float roughness;
    float metallic;
} material;

vec3 getNormalFromMapNoTangent()
{
    vec3 tangentNormal = texture(u_normalMap, inTexCoord).xyz * 2.0 - 1.0;
    
    vec3 Q1  = dFdx(inFragPosDepth.xyz);
    vec3 Q2  = dFdy(inFragPosDepth.xyz);
    vec2 st1 = dFdx(inTexCoord);
    vec2 st2 = dFdy(inTexCoord);

    vec3 N   = normalize(inNormal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

// Function to calculate tangent space normal from normal map using pre-computed TBN
vec3 getNormalFromMap()
{

    vec3 tangentNormal = texture(u_normalMap, inTexCoord).xyz * 2.0 - 1.0;
    
    // Use the pre-calculated tangent and bitangent
    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent);
    vec3 B = normalize(inBitangent);
    
    // Form TBN matrix from pre-computed vectors
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

void main() {


    // Get material properties from textures or fallback to uniforms

    float roughness = material.roughness;
    float metallic = material.metallic;
    float ao = 1.0;

    // Albedo

    vec3 albedo = texture(u_albedoMap, inTexCoord).rgb * material.albedo;
    

    // Roughness
    roughness = roughness * texture(u_metallicRoughnessMap, inTexCoord).g;
    
    // Metallic
    metallic = metallic * texture(u_metallicRoughnessMap, inTexCoord).b;

    // AO
    ao = ao * texture(u_aoMap, inTexCoord).r;

    


    // Position (world space) and Depth (view space Z)
    // Store World Position in rgb, and linear View-Space Z Depth in alpha
    gPositionDepth = inFragPosDepth;
    
    // Normal
    vec3 normal;
    if (length(inTangent) > 0.01) {
        normal = getNormalFromMap();
        if (length(normal) < 0.01) {
            normal = vec3(1.0, 1.0, 1.0);
        }
    } else {
        normal = getNormalFromMapNoTangent();
    }

    gNormal = vec4(normal, 1.0); // Store normal in RGB and 1.0 in Alpha
    
    // Albedo and specular
    gAlbedoSpec = vec4(albedo, 1.0);
    
    // Material properties
    gMaterial = vec4(metallic, roughness, ao, 1.0); // Output vec4 with alpha = 1.0

}
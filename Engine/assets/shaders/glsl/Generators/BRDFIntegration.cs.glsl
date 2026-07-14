#version 450

// Karis split-sum BRDF integration LUT.
// Output: RG16F table indexed by (NdotV, roughness) -> (scale, bias) for the specular env term.
// Scene- and view-independent, so this bakes once ever.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 4, binding = 0, rg16f) uniform writeonly image2D outputTexture;

const float PI = 3.14159265359;
const uint SAMPLE_COUNT = 1024u;

float s_radicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 s_hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), s_radicalInverseVdC(i));
}

vec3 s_importanceSampleGGX(vec2 xi, vec3 n, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, n));
    vec3 bitangent = cross(n, tangent);
    return normalize(tangent * h.x + bitangent * h.y + n * h.z);
}

// Smith geometry with the IBL remap of k (Karis)
float s_geometrySchlickGGX(float nDotV, float roughness) {
    float k = (roughness * roughness) / 2.0;
    return nDotV / (nDotV * (1.0 - k) + k);
}

float s_geometrySmith(float nDotV, float nDotL, float roughness) {
    return s_geometrySchlickGGX(nDotV, roughness) * s_geometrySchlickGGX(nDotL, roughness);
}

vec2 s_integrateBRDF(float nDotV, float roughness) {
    vec3 v = vec3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);
    vec3 n = vec3(0.0, 0.0, 1.0);

    float a = 0.0;
    float b = 0.0;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = s_hammersley(i, SAMPLE_COUNT);
        vec3 h = s_importanceSampleGGX(xi, n, roughness);
        vec3 l = normalize(2.0 * dot(v, h) * h - v);

        float nDotL = max(l.z, 0.0);
        float nDotH = max(h.z, 0.0);
        float vDotH = max(dot(v, h), 0.0);

        if (nDotL > 0.0) {
            float g = s_geometrySmith(nDotV, nDotL, roughness);
            float gVis = (g * vDotH) / (nDotH * nDotV);
            float fc = pow(1.0 - vDotH, 5.0);
            a += (1.0 - fc) * gVis;
            b += fc * gVis;
        }
    }
    return vec2(a, b) / float(SAMPLE_COUNT);
}

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 res = imageSize(outputTexture);
    if (texel.x >= res.x || texel.y >= res.y) {
        return;
    }

    // Texel center maps to (NdotV, roughness) in (0, 1]
    float nDotV = (float(texel.x) + 0.5) / float(res.x);
    float roughness = (float(texel.y) + 0.5) / float(res.y);

    imageStore(outputTexture, texel, vec4(s_integrateBRDF(nDotV, roughness), 0.0, 0.0));
}

#version 450

// Prefiltered specular environment map (Karis split-sum).
// GGX-importance-samples the source environment cube for a fixed roughness (one mip per dispatch)
// and writes the result into the bound mip of the prefiltered cube.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 4, binding = 0) uniform samplerCube srcCube;
layout(set = 4, binding = 1, rgba16f) uniform writeonly image2DArray outputTexture;

layout(push_constant) uniform PushConstants {
    float roughness;
    float srcResolution; // face resolution of the source cube, for mip selection
} pc;

const float PI = 3.14159265359;
const uint SAMPLE_COUNT = 1024u;

vec3 s_getCubeDir(int face, vec2 uv) {
    vec3 dir;
    if (face == 0)      dir = vec3( 1.0, -uv.y, -uv.x);
    else if (face == 1) dir = vec3(-1.0, -uv.y,  uv.x);
    else if (face == 2) dir = vec3( uv.x,  1.0,  uv.y);
    else if (face == 3) dir = vec3( uv.x, -1.0, -uv.y);
    else if (face == 4) dir = vec3( uv.x, -uv.y,  1.0);
    else                dir = vec3(-uv.x, -uv.y, -1.0);
    return normalize(dir);
}

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

float s_distributionGGX(float nDotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

void main() {
    ivec3 texel = ivec3(gl_GlobalInvocationID);
    ivec2 res = imageSize(outputTexture).xy;
    if (texel.x >= res.x || texel.y >= res.y) {
        return;
    }

    vec2 uv = (vec2(texel.xy) + 0.5) / vec2(res) * 2.0 - 1.0;
    // Isotropic assumption: view = reflection = surface normal
    vec3 n = s_getCubeDir(texel.z, uv);
    vec3 v = n;

    vec3 prefiltered = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = s_hammersley(i, SAMPLE_COUNT);
        vec3 h = s_importanceSampleGGX(xi, n, pc.roughness);
        vec3 l = normalize(2.0 * dot(v, h) * h - v);

        float nDotL = max(dot(n, l), 0.0);
        if (nDotL > 0.0) {
            // Mip-biased source sampling to reduce fireflies (Krivanek/Colbert)
            float nDotH = max(dot(n, h), 0.0);
            float vDotH = max(dot(v, h), 0.0);
            float d = s_distributionGGX(nDotH, pc.roughness);
            float pdf = (d * nDotH / (4.0 * vDotH)) + 0.0001;

            float saTexel = 4.0 * PI / (6.0 * pc.srcResolution * pc.srcResolution);
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);
            float mipLevel = pc.roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);

            prefiltered += textureLod(srcCube, l, mipLevel).rgb * nDotL;
            totalWeight += nDotL;
        }
    }

    prefiltered = prefiltered / max(totalWeight, 0.0001);
    imageStore(outputTexture, texel, vec4(prefiltered, 1.0));
}

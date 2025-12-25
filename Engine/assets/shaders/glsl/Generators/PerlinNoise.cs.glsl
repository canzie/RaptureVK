#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 4, binding = 0, rgba8) uniform writeonly image2D outputTexture;

layout(push_constant) uniform PushConstants {
    int octaves;
    float persistence;
    float lacunarity;
    float scale;
    uint seed;
} pc;

// Hash function to generate pseudo-random gradients
uint hash(uint x) {
    x ^= pc.seed;
    x = (x ^ 61u) ^ (x >> 16u);
    x *= 9u;
    x = x ^ (x >> 4u);
    x *= 0x27d4eb2du;
    x = x ^ (x >> 15u);
    return x;
}

// 2D gradient function
vec2 gradient(uint x, uint y) {
    uint h = hash(x + y * 57u) & 7u;
    vec2 grad = vec2(0.0);
    switch (h) {
        case 0u: grad = vec2(1, 1); break;
        case 1u: grad = vec2(-1, 1); break;
        case 2u: grad = vec2(1, -1); break;
        case 3u: grad = vec2(-1, -1); break;
        case 4u: grad = vec2(1, 0); break;
        case 5u: grad = vec2(-1, 0); break;
        case 6u: grad = vec2(0, 1); break;
        case 7u: grad = vec2(0, -1); break;
    }
    return grad;
}

// Fade function for smooth interpolation
float fade(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Linear interpolation
float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

// Perlin noise at point p
float perlin(vec2 p) {
    ivec2 i0 = ivec2(floor(p));
    vec2 f = fract(p);

    float u = fade(f.x);
    float v = fade(f.y);

    float n00 = dot(gradient(uint(i0.x),     uint(i0.y)),     f - vec2(0, 0));
    float n10 = dot(gradient(uint(i0.x + 1), uint(i0.y)),     f - vec2(1, 0));
    float n01 = dot(gradient(uint(i0.x),     uint(i0.y + 1)), f - vec2(0, 1));
    float n11 = dot(gradient(uint(i0.x + 1), uint(i0.y + 1)), f - vec2(1, 1));

    float nx0 = lerp(n00, n10, u);
    float nx1 = lerp(n01, n11, u);
    return lerp(nx0, nx1, v);
}

// Fractal noise
float fractalNoise(vec2 p) {
    float total = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;
    float maxAmplitude = 0.0;

    for (int i = 0; i < pc.octaves; ++i) {
        total += perlin(p * frequency) * amplitude;
        maxAmplitude += amplitude;
        amplitude *= pc.persistence;
        frequency *= pc.lacunarity;
    }

    return total / maxAmplitude;
}

void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outputTexture);

    if (pixelCoords.x >= size.x || pixelCoords.y >= size.y)
        return;

    vec2 uv = vec2(pixelCoords) / vec2(size);
    vec2 p = uv * pc.scale;

    float noise = fractalNoise(p);
    noise = noise * 0.5 + 0.5; // Normalize to [0, 1]

    imageStore(outputTexture, pixelCoords, vec4(noise, noise, noise, 1.0));
}
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 4, binding = 0, rgba8) uniform writeonly image2D outImage;

// Annotations: @hidden, @range(min,max), @default(val), @name("Display Name")
layout(push_constant) uniform RidgedNoisePushConstants {
    int octaves;            // @name("Octaves") @range(1, 8) @default(4)
    float persistence;       // @name("Persistence") @range(0.1, 1.0) @default(0.5)
    float lacunarity;        // @name("Lacunarity") @range(1.0, 4.0) @default(2.0)
    float scale;             // @name("Scale") @range(0.5, 32.0) @default(4.0)
    float ridgeExponent;     // @name("Ridge Sharpness") @range(0.1, 4.0) @default(2.0)
    float amplitudeMultiplier; // @name("Amplitude") @range(0.1, 5.0) @default(1.0)
    uint seed;               // @name("Seed") @range(0, 65535) @default(42)
} pc;

vec3 mod289(vec3 x) {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec2 mod289(vec2 x) {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec3 permute(vec3 x) {
    return mod289(((x * 34.0) + 1.0) * x);
}

float snoise(vec2 v) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439, -0.577350269189626, 0.024390243902439);
    vec2 i = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1;
    i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod289(i);
    vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)), 0.0);
    m = m * m;
    m = m * m;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}
float ridgedFbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;
    float maxAmplitude = 0.0;

    for (int i = 0; i < pc.octaves; i++) {
        // standard ridged noise
        float n = 1.0 - abs(snoise(p * frequency));
        n = pow(n, pc.ridgeExponent); // softer peaks
        value += amplitude * n;

        maxAmplitude += amplitude;

        frequency *= pc.lacunarity;
        amplitude *= pc.persistence;
    }

    // normalize to [0,1]
    value *= pc.amplitudeMultiplier;
    value /= maxAmplitude;

    return value;
}
float _ridgedFbm(vec2 p) {
    float value = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;

    for (int i = 0; i < pc.octaves; i++) {
        float n = abs(snoise(p * frequency));
        n = 1.0 - n;
        n = n * n;
        value += amplitude * n;
        frequency *= pc.lacunarity;
        amplitude *= pc.persistence;
    }

    return value;
}

void main() {
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outImage);

    if (texelCoord.x >= size.x || texelCoord.y >= size.y) {
        return;
    }

    vec2 uv = vec2(texelCoord) / vec2(size);
    vec2 p = uv * pc.scale + vec2(float(pc.seed) * 0.1);

    float noise = ridgedFbm(p);

    vec4 color = vec4(vec3(noise), 1.0);
    imageStore(outImage, texelCoord, color);
}

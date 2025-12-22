#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 4, binding = 0, rgba8) uniform writeonly image2D outputTexture;

layout(push_constant) uniform PushConstants {
    uint seed;
} pc;

// PCG hash for high-quality pseudo-random numbers
uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Generate random float in [0, 1]
float randomFloat(uvec2 coord, uint seed) {
    uint h = pcg(coord.x + pcg(coord.y + pcg(seed)));
    return float(h) / float(0xFFFFFFFFu);
}

void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outputTexture);

    if (pixelCoords.x >= size.x || pixelCoords.y >= size.y)
        return;

    float noise = randomFloat(uvec2(pixelCoords), pc.seed);
    imageStore(outputTexture, pixelCoords, vec4(noise, noise, noise, 1.0));
}

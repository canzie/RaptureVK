#version 450

// Compile-time format selection (exactly one must be defined):
//   COMPRESS_BC1 - RGB, 8 bytes/block
//   COMPRESS_BC4 - single channel, 8 bytes/block
//   COMPRESS_BC3 - RGBA (BC1 color + BC4 alpha), 16 bytes/block
//   COMPRESS_BC5 - two channel (two BC4 blocks), 16 bytes/block

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 4, binding = 0) uniform sampler2D srcTexture;

layout(std430, set = 4, binding = 1) buffer OutputBlocks {
    uint blocks[];
};

layout(push_constant) uniform PushConstants {
    uint mipWidth;
    uint mipHeight;
    uint mipLod;
    uint blockOffset;
} pc;

#if defined(COMPRESS_BC3) || defined(COMPRESS_BC5)
    #define BLOCK_UINTS 4u
#else
    #define BLOCK_UINTS 2u
#endif

uint pack565(vec3 c) {
    uint r = uint(round(clamp(c.r, 0.0, 1.0) * 31.0));
    uint g = uint(round(clamp(c.g, 0.0, 1.0) * 63.0));
    uint b = uint(round(clamp(c.b, 0.0, 1.0) * 31.0));
    return (r << 11) | (g << 5) | b;
}

vec3 unpack565(uint v) {
    uint r = (v >> 11) & 31u;
    uint g = (v >> 5) & 63u;
    uint b = v & 31u;
    return vec3(float(r) / 31.0, float(g) / 63.0, float(b) / 31.0);
}

// ORs numbits of value into a 64-bit field split across (lo, hi) at the given bit position
void setBits(inout uint lo, inout uint hi, uint value, uint bitpos, uint numbits) {
    if (bitpos < 32u) {
        lo |= value << bitpos;
        if (bitpos + numbits > 32u) {
            hi |= value >> (32u - bitpos);
        }
    } else {
        hi |= value << (bitpos - 32u);
    }
}

void encodeBC1Color(vec3 colors[16], out uint out0, out uint out1) {
    vec3 minC = colors[0];
    vec3 maxC = colors[0];
    for (int i = 1; i < 16; ++i) {
        minC = min(minC, colors[i]);
        maxC = max(maxC, colors[i]);
    }

    vec3 inset = (maxC - minC) / 16.0;
    minC = clamp(minC + inset, 0.0, 1.0);
    maxC = clamp(maxC - inset, 0.0, 1.0);

    uint c0 = pack565(maxC);
    uint c1 = pack565(minC);

    if (c0 < c1) {
        uint tmp = c0;
        c0 = c1;
        c1 = tmp;
    }

    uint indices = 0u;
    if (c0 != c1) {
        vec3 e0 = unpack565(c0);
        vec3 e1 = unpack565(c1);
        vec3 pal[4];
        pal[0] = e0;
        pal[1] = e1;
        pal[2] = (2.0 * e0 + e1) / 3.0;
        pal[3] = (e0 + 2.0 * e1) / 3.0;

        for (int i = 0; i < 16; ++i) {
            float bestDist = 1e9;
            uint best = 0u;
            for (uint j = 0u; j < 4u; ++j) {
                vec3 d = colors[i] - pal[j];
                float dist = dot(d, d);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = j;
                }
            }
            indices |= best << (2u * uint(i));
        }
    }

    out0 = c0 | (c1 << 16);
    out1 = indices;
}

void encodeBC4(float values[16], out uint out0, out uint out1) {
    float minV = values[0];
    float maxV = values[0];
    for (int i = 1; i < 16; ++i) {
        minV = min(minV, values[i]);
        maxV = max(maxV, values[i]);
    }

    uint r0 = uint(round(clamp(maxV, 0.0, 1.0) * 255.0));
    uint r1 = uint(round(clamp(minV, 0.0, 1.0) * 255.0));

    uint lo = 0u;
    uint hi = 0u;
    setBits(lo, hi, r0, 0u, 8u);
    setBits(lo, hi, r1, 8u, 8u);

    if (r0 != r1) {
        float ramp[8];
        ramp[0] = float(r0);
        ramp[1] = float(r1);
        for (uint j = 1u; j < 7u; ++j) {
            ramp[j + 1u] = (float(7u - j) * float(r0) + float(j) * float(r1)) / 7.0;
        }

        for (int i = 0; i < 16; ++i) {
            float v = clamp(values[i], 0.0, 1.0) * 255.0;
            float bestDist = 1e9;
            uint best = 0u;
            for (uint j = 0u; j < 8u; ++j) {
                float d = abs(v - ramp[j]);
                if (d < bestDist) {
                    bestDist = d;
                    best = j;
                }
            }
            setBits(lo, hi, best, 16u + 3u * uint(i), 3u);
        }
    }

    out0 = lo;
    out1 = hi;
}

void main() {
    uint blocksX = (pc.mipWidth + 3u) / 4u;
    uint blocksY = (pc.mipHeight + 3u) / 4u;

    uvec2 blockCoord = gl_GlobalInvocationID.xy;
    if (blockCoord.x >= blocksX || blockCoord.y >= blocksY) {
        return;
    }

    vec4 texels[16];
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            ivec2 coord = ivec2(blockCoord * 4u) + ivec2(x, y);
            coord = clamp(coord, ivec2(0), ivec2(pc.mipWidth - 1u, pc.mipHeight - 1u));
            texels[y * 4 + x] = texelFetch(srcTexture, coord, int(pc.mipLod));
        }
    }

    uint blockIndex = blockCoord.y * blocksX + blockCoord.x;
    uint base = pc.blockOffset + blockIndex * BLOCK_UINTS;

#if defined(COMPRESS_BC1)
    vec3 colors[16];
    for (int i = 0; i < 16; ++i) {
        colors[i] = texels[i].rgb;
    }
    uint c0, c1;
    encodeBC1Color(colors, c0, c1);
    blocks[base + 0u] = c0;
    blocks[base + 1u] = c1;

#elif defined(COMPRESS_BC4)
    float values[16];
    for (int i = 0; i < 16; ++i) {
        values[i] = texels[i].r;
    }
    uint b0, b1;
    encodeBC4(values, b0, b1);
    blocks[base + 0u] = b0;
    blocks[base + 1u] = b1;

#elif defined(COMPRESS_BC3)
    float alpha[16];
    vec3 colors[16];
    for (int i = 0; i < 16; ++i) {
        alpha[i] = texels[i].a;
        colors[i] = texels[i].rgb;
    }
    uint a0, a1, c0, c1;
    encodeBC4(alpha, a0, a1);
    encodeBC1Color(colors, c0, c1);
    blocks[base + 0u] = a0;
    blocks[base + 1u] = a1;
    blocks[base + 2u] = c0;
    blocks[base + 3u] = c1;

#elif defined(COMPRESS_BC5)
    float red[16];
    float green[16];
    for (int i = 0; i < 16; ++i) {
        red[i] = texels[i].r;
        green[i] = texels[i].g;
    }
    uint r0, r1, g0, g1;
    encodeBC4(red, r0, r1);
    encodeBC4(green, g0, g1);
    blocks[base + 0u] = r0;
    blocks[base + 1u] = r1;
    blocks[base + 2u] = g0;
    blocks[base + 3u] = g1;
#endif
}

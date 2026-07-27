#version 460

#extension GL_EXT_nonuniform_qualifier : require

// Defined for the Hi-Z pyramid, where a level must bound the nearest surface of everything below it.
// The colour chain leaves it undefined and averages, which is the ordinary box mip.
#ifdef DOWNSAMPLE_REDUCTION_MIN
    #define IMAGE_FORMAT r32f
#else
    #define IMAGE_FORMAT rgba16f
#endif

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, IMAGE_FORMAT) uniform restrict writeonly image2D outputMip;

layout(push_constant) uniform PushConstants {
    uint sourceTextureIndex;
    int sourceMip;
    ivec2 sourceSize;
    ivec2 outputSize;
} pc;

void main() {
    ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(dst, pc.outputSize))) {
        return;
    }

    // Halving an odd dimension leaves one row or column outside the 2x2 footprint. A min pyramid
    // cannot drop it without under-reporting the nearest surface and producing false hits in the
    // trace, so the footprint grows to 3 along any odd axis.
    ivec2 extra = ivec2(notEqual(pc.sourceSize & ivec2(1), ivec2(0)));
    ivec2 src = dst * 2;

#ifdef DOWNSAMPLE_REDUCTION_MIN
    vec4 result = vec4(1.0 / 0.0);
#else
    vec4 result = vec4(0.0);
    float weight = 0.0;
#endif

    for (int y = 0; y <= extra.y + 1; ++y) {
        for (int x = 0; x <= extra.x + 1; ++x) {
            ivec2 coord = min(src + ivec2(x, y), pc.sourceSize - 1);
            vec4 texel = texelFetch(gTextures[nonuniformEXT(pc.sourceTextureIndex)], coord, pc.sourceMip);
#ifdef DOWNSAMPLE_REDUCTION_MIN
            result = min(result, texel);
#else
            result += texel;
            weight += 1.0;
#endif
        }
    }

#ifndef DOWNSAMPLE_REDUCTION_MIN
    result /= weight;
#endif

    imageStore(outputMip, dst, result);
}

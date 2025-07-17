#version 460

#extension GL_EXT_nonuniform_qualifier : require

#if defined(DATA_TYPE_UINT)
    #define SAMPLER_TYPE usampler2DArray
    #define VEC_TYPE uvec4
    #define VEC_CONV(v) vec4(v)
#elif defined(DATA_TYPE_INT)
    #define SAMPLER_TYPE isampler2DArray
    #define VEC_TYPE ivec4
    #define VEC_CONV(v) vec4(v)
#else // Default to FLOAT
    #define SAMPLER_TYPE sampler2DArray
    #define VEC_TYPE vec4
    #define VEC_CONV(v) v
#endif


layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// Bindless texture arrays (set 3)
layout(set = 3, binding = 0) uniform SAMPLER_TYPE gTextures[];

// custom set 
layout(set = 4, binding = 0, rgba32f) uniform restrict image2D outputTex;

// Push constants for dimensions and texture indices
layout (push_constant) uniform PushConstants {
    uint inputTextureIndex;  // Index into bindless texture array
    int layerCount;          // Number of layers in the texture array
    int layerWidth;          // Width of each layer
    int layerHeight;         // Height of each layer
    int tilesPerRow;         // Number of tiles to arrange horizontally
};

void main() {
    // Get the current pixel coordinates
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    
    // Calculate which tile (layer) this pixel belongs to
    int tileX = pixelCoord.x / layerWidth;
    int tileY = pixelCoord.y / layerHeight;
    int layerIndex = tileY * tilesPerRow + tileX;
    
    // If we're outside the valid layers, return early
    if (layerIndex >= layerCount) {
        return;
    }
    
    // Calculate the local coordinates within the tile
    ivec2 localCoord;
    localCoord.x = pixelCoord.x % layerWidth;
    localCoord.y = pixelCoord.y % layerHeight;
    

    
    // Convert local integer coordinates to normalized UVs for sampling
    vec2 uv = (vec2(localCoord) + 0.5) / vec2(layerWidth, layerHeight);
    
    VEC_TYPE sampledValue = texture(gTextures[inputTextureIndex], vec3(uv, layerIndex));
    vec4 finalColor = VEC_CONV(sampledValue);
    
    // Write to the output texture
    imageStore(outputTex, pixelCoord, finalColor);
}

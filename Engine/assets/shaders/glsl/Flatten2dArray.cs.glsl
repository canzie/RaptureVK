#version 460

#extension GL_EXT_nonuniform_qualifier : require


layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// Bindless texture arrays (set 3)
layout(set = 3, binding = 0) uniform sampler2DArray gTextures[];

layout(set = 3, binding = 8, rgba32f) uniform restrict image2D outputTex;

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
    

    
    // Note: We're treating the input as a 2D texture array but accessing via bindless
    // The input texture index should point to the flattened representation
    vec3 sampledColor = texture(gTextures[inputTextureIndex], vec3(localCoord, layerIndex)).xyz;
    
    // Write to the output texture
    imageStore(outputTex, pixelCoord, vec4(sampledColor, 1.0));
}

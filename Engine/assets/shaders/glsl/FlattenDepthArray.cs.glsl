#version 460

#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// Bindless texture arrays (set 3)
layout(set = 3, binding = 0) uniform sampler2DArray gTextures[];

// Fixed output storage binding (set 3, binding 10) for depth textures
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
        // Fill with a dark color to indicate unused areas
        imageStore(outputTex, pixelCoord, vec4(0.1, 0.1, 0.1, 1.0));
        return;
    }
    
    // Calculate the local coordinates within the tile
    ivec2 localCoord;
    localCoord.x = pixelCoord.x % layerWidth;
    localCoord.y = pixelCoord.y % layerHeight;
    

    // Convert local integer coordinates to normalized UVs for sampling
    vec2 uv = (vec2(localCoord) + 0.5) / vec2(layerWidth, layerHeight);

    // Sample depth value from the bindless texture array
    float depthValue = texture(gTextures[inputTextureIndex], vec3(uv, layerIndex)).r;
    
    
    // Apply logarithmic mapping for better depth visualization
    float visualizedDepth = 1.0 - depthValue; // Invert so closer = brighter
    
    // Apply contrast enhancement for better visibility
    visualizedDepth = pow(visualizedDepth, 0.3); // Gamma correction for better contrast
    
    // Ensure we have a visible range
    visualizedDepth = clamp(visualizedDepth, 0.0, 1.0);
    
    // Write to the output texture as grayscale
    imageStore(outputTex, pixelCoord, vec4(visualizedDepth, visualizedDepth, visualizedDepth, 1.0));
} 
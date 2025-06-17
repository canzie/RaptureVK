#version 460

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// Input depth texture array
layout(set=0, binding = 0) uniform sampler2DArray inputDepthArray;

// Output flattened texture (color format for visualization)
layout(set=0, binding = 1, rgba32f) uniform image2D outputTex;

// Push constants for dimensions
layout (push_constant) uniform PushConstants {
    int layerCount;      // Number of layers in the texture array
    int layerWidth;      // Width of each layer
    int layerHeight;     // Height of each layer
    int tilesPerRow;     // Number of tiles to arrange horizontally
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
    
    // Sample depth value from the texture array using texelFetch
    float depthValue = texelFetch(inputDepthArray, ivec3(localCoord, layerIndex), 0).r;
    
    // Convert depth to visible grayscale
    // Depth values are typically 0.0 (far) to 1.0 (near)
    // We'll remap them for better visualization
    
    // Apply logarithmic mapping for better depth visualization
    float visualizedDepth = 1.0 - depthValue; // Invert so closer = brighter
    
    // Apply contrast enhancement for better visibility
    visualizedDepth = pow(visualizedDepth, 0.3); // Gamma correction for better contrast
    
    // Ensure we have a visible range
    visualizedDepth = clamp(visualizedDepth, 0.0, 1.0);
    
    // Write to the output texture as grayscale
    imageStore(outputTex, pixelCoord, vec4(visualizedDepth, visualizedDepth, visualizedDepth, 1.0));
} 
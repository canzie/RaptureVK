#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// The final full-screen output image
layout(set = 4, binding = 0, rgba16f) uniform restrict writeonly image2D u_outputImage;

// The texture array containing the final merged cascade data (Cascade 0)
layout(set = 3, binding = 0) uniform sampler2D gTextureArrays[];

#include "RCCommon.glsl"

// Cascade UBO to get info about Cascade 0
layout(std140, set = 0, binding = 7) uniform CascadeLevelInfos {
    CascadeLevelInfo cascadeLevelInfo;
} cs[];

#define PI 3.14159265359



void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imageSize = imageSize(u_outputImage);

    if (pixelCoords.x >= imageSize.x || pixelCoords.y >= imageSize.y) {
        return;
    }

    // --- Direct Screen-to-Grid Mapping (from your original shader) ---
    CascadeLevelInfo cascade0Info = cs[0].cascadeLevelInfo;
    
    // Get normalized screen coordinates (0.0 to 1.0)
    vec2 uv = (vec2(pixelCoords) + 0.5) / vec2(imageSize);

    // probe coordinate in the current cascade
    ivec2 probeCoord = ivec2(pixelCoords) / ivec2(cascade0Info.angularResolution);

    // this makes sure that when we floor, we end up at the correct topleft.
    vec2 probeCoordF = vec2(probeCoord) - 0.5;
    ivec2 probeBaseCoords = ivec2(floor(probeCoordF));

    vec2 w = fract(probeCoordF); 

    ivec2 offsets[4];
    offsets[0] = ivec2(0, 0); 
    offsets[1] = ivec2(1, 0);
    offsets[2] = ivec2(0, 1);
    offsets[3] = ivec2(1, 1);

    vec3 final_irradiance = vec3(0.0);
    uint angularRes = cascade0Info.angularResolution;
    uint numDirections = angularRes * angularRes;
    

    for (uint dir_index = 0; dir_index < numDirections; dir_index++) {
        ivec2 dir_coord_2d = ivec2(dir_index % angularRes, dir_index / angularRes);

        // --- Bilinearly Interpolate Radiance for THIS RAY ---
        vec3 si[4]; // 4 samples for the 4 probes

        // For each of the 4 surrounding probes...
        for (int probeIdx = 0; probeIdx < 4; probeIdx++) {
            ivec2 probe_index = probeBaseCoords + offsets[probeIdx];

            // Clamp to avoid out-of-bounds access
            probe_index = clamp(probe_index, ivec2(0), cascade0Info.probeGridDimensions - 1);

            // Calculate the final texel coordinate for this probe and ray
            ivec2 texel_coord = probe_index * int(angularRes) + dir_coord_2d;
            
            // Fetch the stored radiance for this probe and this ray
            si[probeIdx] = texelFetch(gTextureArrays[cascade0Info.cascadeTextureIndex], texel_coord, 0).rgb;
        }

        // Bilinearly interpolate the 4 radiance samples
        vec3 interpolated_radiance_x1 = mix(si[0], si[1], w.x);
        vec3 interpolated_radiance_x2 = mix(si[2], si[3], w.x);
        vec3 final_interpolated_radiance = mix(interpolated_radiance_x1, interpolated_radiance_x2, w.y);
        
        final_irradiance += final_interpolated_radiance ;
    }

    // Final normalization for cosine-weighted importance sampling
    final_irradiance /=  float(numDirections);

    imageStore(u_outputImage, pixelCoords, vec4(final_irradiance, 1.0));
}
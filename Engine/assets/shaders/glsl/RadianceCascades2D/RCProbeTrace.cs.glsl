#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : require

#define DEBUG_COORDS 0
#define DEBUG_RAY_INDEX 0
#define DEBUG_PROBE_COORDS 0
#define DEBUG_PROBE_WORLD_POSITION 0
#define DEBUG_RAY_DIRECTION 0


layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Ray data output texture (fixed binding in set 3)
layout(set = 4, binding = 0, rgba32f) uniform restrict writeonly image2D CascadeTextures;

precision highp float;

#include "RCCommon.glsl"
#include "TraceRay.glsl"

#ifndef PI
#define PI 3.14159265358979323846
#endif


// Previous probe data (bindless textures in set 3)
layout(set = 3, binding = 0) uniform samplerCube gCubemaps[];





// Push constants (updated structure)
layout(push_constant) uniform PushConstants {
    uint u_cascadeIndex;
    uint u_cascadeLevels;
    uint u_tlasIndex;
    uint u_lightCount;
    uint u_skyboxTextureIndex;
};



layout(std140, set = 0, binding = 7) uniform CascadeLevelInfos {
    CascadeLevelInfo cascadeLevelInfo;
} cs[];





vec2 getUniformDirection(uint sampleIndex, uint numSamples) {
    float angle = (2.0 * PI * (sampleIndex + 0.5)) / numSamples;
    return normalize(vec2(cos(angle), sin(angle)));
}


struct LightInfo {

    vec4 position;      // w = light type (0 = point, 1 = directional, 2 = spot)
    vec4 direction;     // w = range
    vec4 color;         // w = intensity
    vec4 spotAngles;    // x = inner cone cos, y = outer cone cos, z = entity id, w = unused

};

layout(std140, set=0, binding = 1) uniform LightInfoUBO { 
    LightInfo light;
}u_lights[];




ivec2 getProbeCoords(uvec2 outputCoords, uint angularResolution) {
    ivec2 probeCoords;
    probeCoords.x = int(outputCoords.x / angularResolution);
    probeCoords.y = int(outputCoords.y / angularResolution);

    return probeCoords;
}



void main() {
    CascadeLevelInfo cascadeLevelInfo = cs[u_cascadeIndex].cascadeLevelInfo;
    uint angularResolution = cascadeLevelInfo.angularResolution;
    
    uvec2 outputCoords = uvec2(gl_GlobalInvocationID.xy);
    uvec2 localCoords = uvec2(gl_LocalInvocationID.xy);
    

    // The size of the output texture array
    uvec2 imageSize = uvec2(imageSize(CascadeTextures));

    // Discard threads that are outside of the image dimensions.
    // This can happen due to workgroup rounding.
    if (outputCoords.x >= imageSize.x || 
        outputCoords.y >= imageSize.y) {
        return;
    }


    ivec2 dir_coord = ivec2(outputCoords) % ivec2(angularResolution);
    int dir_index = dir_coord.x + dir_coord.y * int(angularResolution);

    vec2 dir = getUniformDirection(dir_index, angularResolution*angularResolution);
    vec2 probeCoords = floor(outputCoords / angularResolution);
    vec2 probeWorldPosition = probeCoords * cascadeLevelInfo.probeSpacing;
    //vec2 gridShift = (cascadeLevelInfo.probeSpacing * vec2((cascadeLevelInfo.probeGridDimensions - 1))) * 0.5;
    //probeWorldPosition -= gridShift;
    //probeWorldPosition += cascadeLevelInfo.probeOrigin;



    rayQueryEXT query;
    rayQueryInitializeEXT(
        query,
        topLevelAS[u_tlasIndex],
        gl_RayFlagsOpaqueEXT,
        0xFF,
        vec3(probeWorldPosition.x, 0.0, probeWorldPosition.y),
        cascadeLevelInfo.minProbeDistance,
        vec3(dir.x, 0.0, dir.y),
        cascadeLevelInfo.maxProbeDistance
    );


    bool hit = false;
    float hitT = 1e30;

    while (rayQueryProceedEXT(query)) {
        if (rayQueryGetIntersectionTypeEXT(query, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            float t = rayQueryGetIntersectionTEXT(query, false);

            if (t < hitT) {
                hitT = t;
                hit = true;
                // Confirm this intersection as the committed one
                rayQueryConfirmIntersectionEXT(query);
            }
        }
    }

    bool isFrontFacing = false;

    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        hitT = rayQueryGetIntersectionTEXT(query, true);
        isFrontFacing = rayQueryGetIntersectionFrontFaceEXT(query, true);
        hit = true;
    } else {
        hit = false;
    }

    // Default to a miss. The hit distance is set to a negative value.
    // The color is black, but it will be overwritten by the skybox for the last cascade,
    // or by the previous cascade's data during the merge step.
    vec4 outColor = vec4(0.0, 0.0, 0.0, 0.0);

    if (hit) {

            // Get additional ray intersection data
            uint primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
            vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(query, true);
            uint instanceCustomIndex = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);

            MeshInfo meshInfo = u_sceneInfo.MeshInfos[instanceCustomIndex];
            
            // Get surface data for the hit point
            SurfaceData surface = getSurfaceDataForHit(primitiveID, barycentrics, meshInfo);
            
            vec3 emissiveColor = sampleEmissiveColor(meshInfo, surface.texCoord);
            vec3 albedo = sampleAlbedo(meshInfo, surface.texCoord);
            float dstSquared = hitT * hitT;
            float intensity = 1.0 / (hitT + 0.01);

            emissiveColor = emissiveColor * intensity;


            vec3 radiance = emissiveColor;

            outColor = vec4(radiance, 1.0);

    } else { 
        // For the final cascade, sample the skybox on a miss
        if (u_cascadeIndex == u_cascadeLevels - 1) {
            if (u_skyboxTextureIndex != UINT32_MAX) {
                //vec3 skyboxColor = texture(gCubemaps[u_skyboxTextureIndex], probeRayDirection).rgb;
                //outColor.rgb = skyboxColor;
                //outColor.a = 1.0;
                outColor = vec4(0.0, 0.0, 0.0, 1.0);
                
            }
        }
    }


    // Store the resulting color.
    imageStore(CascadeTextures, ivec2(outputCoords), outColor);
}

#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : require

#define RAY_DATA_TEXTURE


layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// Ray data output texture
layout (set=0, binding = 0, rgba32f) uniform restrict writeonly image2DArray RayData;

// Previous probe data
layout (set=0, binding = 1) uniform sampler2DArray prevProbeIrradianceAtlas;
layout (set=0, binding = 2) uniform sampler2DArray prevProbeDistanceAtlas;

// Skybox Cubemap
layout (set=0, binding = 3) uniform samplerCube u_skyboxCubemap;

// TLAS binding
layout (set=0, binding = 4) uniform accelerationStructureEXT topLevelAS;

// Simplified MeshInfo structure matching our C++ version
struct MeshInfo {
    uint AlbedoTextureIndex;
    uint NormalTextureIndex;
    uint MetallicRoughnessTextureIndex;
    uint iboIndex; // index of the buffer in the bindless buffers array
    uint vboIndex; // index of the buffer in the bindless buffers array
    uint meshIndex; // index of the mesh in the mesh array, this is the same index as the tlasinstance instanceCustomIndex
};

// Scene info buffer
layout(std430, set=0, binding = 5) readonly buffer SceneInfo {
    MeshInfo MeshInfos[];
} u_sceneInfo;



#include "ProbeCommon.glsl"
#include "IrradianceCommon.glsl"

// Input Uniforms / Buffers
layout(std140, set=1, binding = 0) uniform ProbeInfo {
    ProbeVolume u_volume;
};

// Sun shadow uniforms
layout(std140, set=1, binding = 1) uniform SunPropertiesUBO {
    SunProperties u_SunProperties;
};





// Ray query for intersection testing
rayQueryEXT rayQuery;

vec3 sampleAlbedo(MeshInfo meshInfo, vec2 uv) {
    if (meshInfo.AlbedoTextureIndex == 0) {
        return vec3(1.0, 0.0, 1.0); // Default color
    }
    return texture(gTextures[meshInfo.AlbedoTextureIndex], uv).rgb;
}

vec3 sampleNormal(MeshInfo meshInfo, vec2 uv) {
    if (meshInfo.NormalTextureIndex == 0) {
        return vec3(0.0, 0.0, 0.0);
    }
    return texture(gTextures[meshInfo.NormalTextureIndex], uv).rgb;
}

vec3 calculateShadingNormal(
    MeshInfo meshInfo,
    vec2 uv,
    vec3 N_geom,   // Interpolated geometric normal (world space)
    vec3 T_geom    // Interpolated tangent (world space)
) {
    vec3 finalNormal = N_geom;

    if (meshInfo.NormalTextureIndex != 0) {
        vec3 tangentNormal = texture(gTextures[meshInfo.NormalTextureIndex], uv).xyz;
        tangentNormal = normalize(tangentNormal * 2.0 - 1.0);

        vec3 T = normalize(T_geom - dot(T_geom, N_geom) * N_geom);
        vec3 B = normalize(cross(N_geom, T));
        mat3 TBN = mat3(T, B, N_geom);

        finalNormal = normalize(TBN * tangentNormal);
    }

    return finalNormal;
}

void main() {
    ivec3 probeCoords = ivec3(gl_WorkGroupID.x, gl_WorkGroupID.z, gl_WorkGroupID.y);
    int probeIndex = DDGIGetProbeIndex(probeCoords, u_volume);
    int rayIndex = int(gl_LocalInvocationID.y * gl_WorkGroupSize.x + gl_LocalInvocationID.x);

    vec3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, u_volume);
    vec3 probeRayDirection = DDGIGetProbeRayDirection(rayIndex, u_volume);
    uvec3 outputCoords = DDGIGetRayDataTexelCoords(rayIndex, probeIndex, u_volume);


    // Initialize ray query
    rayQueryInitializeEXT(rayQuery, topLevelAS, gl_RayFlagsOpaqueEXT, 0xFF, 
        probeWorldPosition, 0.0, probeRayDirection, u_volume.probeMaxRayDistance*1.5);


    


    // Trace the ray
    bool hit = false;
    vec3 hitPosition;
    vec3 hitNormal;
    vec2 hitUV;
    uint hitInstanceID;
    float hitT;
    bool isFrontFacing = true;

    while(rayQueryProceedEXT(rayQuery)) {
        if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            isFrontFacing = !rayQueryGetIntersectionFrontFaceEXT(rayQuery, false);
            
            if (isFrontFacing) {
                rayQueryConfirmIntersectionEXT(rayQuery);
            }
        }
    }

    if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        hit = true;
        hitT = rayQueryGetIntersectionTEXT(rayQuery, true);
        hitPosition = probeWorldPosition + probeRayDirection * hitT;
        hitInstanceID = rayQueryGetIntersectionInstanceIdEXT(rayQuery, true);
        
        if (isFrontFacing) {
            // Front face hit - store red color for visualization
            MeshInfo meshInfo = u_sceneInfo.MeshInfos[hitInstanceID];
            
            // Use the mesh index to verify we're accessing the correct mesh data
            // For now, just store a red color to indicate a hit
            vec3 hitColor = vec3(1.0, 0.0, 0.0); // Red for hits 
            DDGIStoreProbeRayFrontfaceHit(ivec3(outputCoords), hitColor, hitT);
        } else {
            // Back face hit - store negative distance
            DDGIStoreProbeRayBackfaceHit(ivec3(outputCoords), hitT);
        }
    } else {
        // Miss - sample skybox and store blue color for visualization
        //vec3 skyboxColor = texture(u_skyboxCubemap, probeRayDirection).rgb;
        vec3 skyboxColor = vec3(0.0, 0.0, 0.0);
        // If no skybox, use green color for misses
        if (length(skyboxColor) < 0.001) {
            skyboxColor = vec3(0.0, 1.0, 0.0); // Green for misses
        } else {
            skyboxColor = vec3(0.0, 0.0, 1.0); // Blue for skybox samples
        }
        
        DDGIStoreProbeRayMiss(ivec3(outputCoords), skyboxColor);
    }
    
}

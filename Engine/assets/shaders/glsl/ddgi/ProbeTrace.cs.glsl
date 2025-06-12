#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : require


#include "ProbeCommon.glsl"
#include "MeshCommon.glsl"
#include "IrradianceCommon.glsl"


layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// Ray data output texture
layout (binding = 0, rgba32f) uniform restrict writeonly image2DArray RayData;

// Previous probe data
layout (binding = 1) uniform sampler2DArray prevProbeAtlas;
layout (binding = 2) uniform sampler2DArray prevProbeDepthAtlas;

// Skybox Cubemap
layout (binding = 3) uniform samplerCube u_skyboxCubemap;

// TLAS binding
layout (binding = 4) uniform accelerationStructureEXT topLevelAS;

// Input Uniforms / Buffers
layout(std140, binding = 0) uniform ProbeInfo {
    ProbeVolume u_volume;
};

// Sun shadow uniforms
layout(std140, binding = 1) uniform SunPropertiesUBO {
    SunProperties u_SunProperties;
};

// Scene info buffer
layout(std430, binding = 2) readonly buffer SceneInfo {
    MeshInfo MeshInfos[];
} u_sceneInfo;

// Buffer metadata storage
layout(std430, binding = 3) readonly buffer BufferMetadataStorage {
    BufferMetadata AllBufferMetadata[];
} u_bufferMetadata;

precision highp float;

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
    bool isFrontFacing;

    while(rayQueryProceedEXT(rayQuery)) {
        if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            isFrontFacing = !rayQueryGetIntersectionFrontFaceEXT(rayQuery, false);
            
            if (!isFrontFacing) {
                rayQueryConfirmIntersectionEXT(rayQuery);
            }
        }
    }

    if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        hit = true;
        hitT = rayQueryGetIntersectionTEXT(rayQuery, true);
        hitPosition = probeWorldPosition + probeRayDirection * hitT;
        hitInstanceID = rayQueryGetIntersectionInstanceIdEXT(rayQuery, true);
        
        // Get barycentric coordinates
        vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(rayQuery, true);
        float w = 1.0 - barycentrics.x - barycentrics.y;
        
        // Get primitive ID and instance
        uint primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
        MeshInfo meshInfo = u_sceneInfo.MeshInfos[hitInstanceID];
        
        /*
        // Get triangle vertices and attributes
        BufferMetadata metadata = u_bufferMetadata.AllBufferMetadata[meshInfo.bufferMetadataIDX];
        Triangle tri = getTriangleExtras(meshInfo, primitiveID, metadata);
        
        // Interpolate attributes
        hitUV = tri.uv0 * w + tri.uv1 * barycentrics.x + tri.uv2 * barycentrics.y;
        vec3 worldNormal_geom = normalize(tri.n0 * w + tri.n1 * barycentrics.x + tri.n2 * barycentrics.y);
        vec3 worldTangent_geom = normalize(tri.t0 * w + tri.t1 * barycentrics.x + tri.t2 * barycentrics.y);
        
        // Calculate final normal
        vec3 worldShadingNormal = calculateShadingNormal(meshInfo, hitUV, worldNormal_geom, worldTangent_geom);
        vec3 albedo = sampleAlbedo(meshInfo, hitUV);

        if (!isFrontFacing) {
            // Store backface hit
            DDGIStoreProbeRayBackfaceHit(ivec3(outputCoords), hitT);
            return;
        }

        // Calculate lighting
        vec3 diffuse = DirectDiffuseLighting(albedo, worldShadingNormal, hitPosition, u_SunProperties);

        // Calculate indirect lighting
        vec3 irradiance = vec3(0.0);
        vec3 surfaceBias = DDGIGetSurfaceBias(worldShadingNormal, probeRayDirection, u_volume);

        irradiance = DDGIGetVolumeIrradiance(
            hitPosition,
            worldShadingNormal,
            surfaceBias,
            prevProbeAtlas,
            prevProbeDepthAtlas,
            u_volume);

        float maxAlbedo = 0.9;
        vec3 radiance = diffuse + ((min(albedo, vec3(maxAlbedo)) / PI) * irradiance);
        
        // Store the result
        DDGIStoreProbeRayFrontfaceHit(ivec3(outputCoords), clamp(radiance, vec3(0.0), vec3(1.0)), hitT);
    */
        } else {
        // Sample skybox for misses
        //vec3 sunColor = vec3(1.0, 1.0, 1.0);
        //sunColor *= u_SunProperties.sunIntensity * u_SunProperties.sunColor;
        //DDGIStoreProbeRayMiss(ivec3(outputCoords), sunColor * u_SunProperties.sunIntensity);
    }
}

#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : require

#define RAY_DATA_TEXTURE
#define DDGI_ENABLE_DIFFUSE_LIGHTING

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// Ray data output texture
layout (set=0, binding = 0, rgba32f) uniform restrict writeonly image2DArray RayData;

// Previous probe data
layout (set=2, binding = 0) uniform sampler2DArray prevProbeIrradianceAtlas;
layout (set=2, binding = 1) uniform sampler2DArray prevProbeDistanceAtlas;

// Skybox Cubemap
layout (set=0, binding = 3) uniform samplerCube u_skyboxCubemap;

// TLAS binding
layout (set=0, binding = 4) uniform accelerationStructureEXT topLevelAS;

// includes index and vertex buffers, the meshinfo contains the offsets into these buffers
layout(set = 3, binding = 1) readonly buffer gBindlessBuffers {
    uint data[];
} gBuffers[];

precision highp float;


// Simplified MeshInfo structure matching our C++ version
struct MeshInfo {
    uint AlbedoTextureIndex;
    uint NormalTextureIndex;
    uint MetallicRoughnessTextureIndex;
    uint iboIndex; // index of the buffer in the bindless buffers array
    uint vboIndex; // index of the buffer in the bindless buffers array
    uint meshIndex; // index of the mesh in the mesh array, this is the same index as the tlasinstance instanceCustomIndex

    mat4 modelMatrix;

    uint     positionAttributeOffsetBytes; // Offset of position *within* the stride
    uint     texCoordAttributeOffsetBytes;
    uint     normalAttributeOffsetBytes;
    uint     tangentAttributeOffsetBytes;


    uint     vertexStrideBytes;            // Stride of the vertex buffer in bytes
    uint     indexType;                    // GL_UNSIGNED_INT (5125) or GL_UNSIGNED_SHORT (5123)

};

// Scene info buffer
layout(std430, set=0, binding = 5) readonly buffer SceneInfo {
    MeshInfo MeshInfos[];
} u_sceneInfo;

// Global descriptor arrays for bindless textures (set 3)
#ifndef DESCRIPTOR_ARRAYS_DEFINED
#define DESCRIPTOR_ARRAYS_DEFINED
layout(set = 3, binding = 0) uniform sampler2D gTextures[];
layout(set = 3, binding = 0) uniform sampler2DShadow gShadowMaps[];
#endif

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

// Vertex attribute structure for clean data passing
struct VertexAttributes {
    vec3 position;
    vec2 texCoord;
    vec3 normal;
    vec4 tangent;
    vec3 bitangent;
};

// Interpolated surface data
struct SurfaceData {
    vec3 position;
    vec2 texCoord;
    vec3 normal;
    vec4 tangent;
    vec3 bitangent;
};

/**
 * Fetches triangle indices from the index buffer
 */
uvec3 fetchTriangleIndices(uint primitiveID, MeshInfo meshInfo) {
    uint baseIndex = primitiveID * 3;
    uvec3 indices;
    
    if (meshInfo.indexType == 5125) { // GL_UNSIGNED_INT
        uint indexOffset = baseIndex * 4; // 4 bytes per uint
        indices.x = gBuffers[meshInfo.iboIndex].data[indexOffset / 4];
        indices.y = gBuffers[meshInfo.iboIndex].data[(indexOffset + 4) / 4];
        indices.z = gBuffers[meshInfo.iboIndex].data[(indexOffset + 8) / 4];
    } else { // GL_UNSIGNED_SHORT (5123)
        uint indexOffset = baseIndex * 2; // 2 bytes per ushort
        uint packedIndices0 = gBuffers[meshInfo.iboIndex].data[indexOffset / 4];
        uint packedIndices1 = gBuffers[meshInfo.iboIndex].data[(indexOffset + 4) / 4];
        
        // Extract 16-bit indices from packed 32-bit values
        indices.x = packedIndices0 & 0xFFFF;
        indices.y = (packedIndices0 >> 16) & 0xFFFF;
        indices.z = packedIndices1 & 0xFFFF;
    }
    
    return indices;
}

/**
 * Fetches vertex attributes for a single vertex
 */
VertexAttributes fetchVertexAttributes(uint vertexIndex, MeshInfo meshInfo) {
    VertexAttributes attrs;
    uint vertexOffset = vertexIndex * meshInfo.vertexStrideBytes;
    
    // Position (assuming vec3)
    uint posOffset = vertexOffset + meshInfo.positionAttributeOffsetBytes;
    attrs.position = vec3(
        uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[posOffset / 4]),
        uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(posOffset + 4) / 4]),
        uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(posOffset + 8) / 4])
    );
    
    // Texture coordinates (assuming vec2)
    uint texOffset = vertexOffset + meshInfo.texCoordAttributeOffsetBytes;
    attrs.texCoord = vec2(
        uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[texOffset / 4]),
        uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(texOffset + 4) / 4])
    );
    
    // Normal (assuming vec3)
    uint normalOffset = vertexOffset + meshInfo.normalAttributeOffsetBytes;
    attrs.normal = vec3(
        uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[normalOffset / 4]),
        uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(normalOffset + 4) / 4]),
        uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(normalOffset + 8) / 4])
    );
    
    // Tangent (assuming vec4)
    uint tangentOffset = vertexOffset + meshInfo.tangentAttributeOffsetBytes;
    attrs.tangent = vec4(
        uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[tangentOffset / 4]),
        uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(tangentOffset + 4) / 4]),
        uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(tangentOffset + 8) / 4]),
        uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(tangentOffset + 12) / 4])
    );

    attrs.position = (meshInfo.modelMatrix * vec4(attrs.position, 1.0)).xyz;
    attrs.normal = normalize((meshInfo.modelMatrix * vec4(attrs.normal, 0.0)).xyz);
    
    // Transform only the tangent vector (xyz), preserve handedness (w)
    vec3 worldTangent = normalize((meshInfo.modelMatrix * vec4(attrs.tangent.xyz, 0.0)).xyz);
    attrs.tangent = vec4(worldTangent, attrs.tangent.w);
    
    // Re-orthogonalize tangent with respect to normal (in world space)
    vec3 T = normalize(attrs.tangent.xyz - dot(attrs.tangent.xyz, attrs.normal) * attrs.normal);
    attrs.tangent = vec4(T, attrs.tangent.w);
    
    // Calculate bitangent with proper handedness
    attrs.bitangent = normalize(cross(attrs.normal, T) * attrs.tangent.w);
    
    return attrs;
}

/**
 * Interpolates vertex attributes using barycentric coordinates
 */
SurfaceData interpolateVertexAttributes(VertexAttributes v0, VertexAttributes v1, VertexAttributes v2, vec2 barycentrics) {
    vec3 weights = vec3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    
    SurfaceData surface;
    surface.position = v0.position * weights.x + v1.position * weights.y + v2.position * weights.z;
    surface.texCoord = v0.texCoord * weights.x + v1.texCoord * weights.y + v2.texCoord * weights.z;
    surface.normal = normalize(v0.normal * weights.x + v1.normal * weights.y + v2.normal * weights.z);
    
    vec4 interpolatedTangent = v0.tangent * weights.x + v1.tangent * weights.y + v2.tangent * weights.z;
    surface.tangent = vec4(normalize(interpolatedTangent.xyz), interpolatedTangent.w);
    
    // Interpolate bitangent
    surface.bitangent = normalize(v0.bitangent * weights.x + v1.bitangent * weights.y + v2.bitangent * weights.z);
    
    // Calculate bitangent using the same approach as GBuffer shader
    // Re-orthogonalize tangent with respect to normal (in world space)
    vec3 T = normalize(surface.tangent.xyz - dot(surface.tangent.xyz, surface.normal) * surface.normal);
    surface.tangent = vec4(T, surface.tangent.w);
    
    // Calculate bitangent with proper handedness
    surface.bitangent = normalize(cross(surface.normal, T) * surface.tangent.w);
    
    return surface;
}

/**
 * Gets complete surface data for a ray hit
 */
SurfaceData getSurfaceDataForHit(uint primitiveID, vec2 barycentrics, MeshInfo meshInfo) {
    // Fetch triangle indices
    uvec3 indices = fetchTriangleIndices(primitiveID, meshInfo);
    
    // Fetch vertex data for all 3 vertices
    VertexAttributes v0 = fetchVertexAttributes(indices.x, meshInfo);
    VertexAttributes v1 = fetchVertexAttributes(indices.y, meshInfo);
    VertexAttributes v2 = fetchVertexAttributes(indices.z, meshInfo);
    
    // Interpolate attributes
    return interpolateVertexAttributes(v0, v1, v2, barycentrics);
}

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
    vec4 T_geom,   // Interpolated tangent (world space, w component contains handedness)
    vec3 B_geom    // Pre-computed bitangent (world space)
) {
    vec3 finalNormal = N_geom;

    if (meshInfo.NormalTextureIndex != 0) {
        vec3 tangentNormal = texture(gTextures[meshInfo.NormalTextureIndex], uv).xyz;
        tangentNormal = tangentNormal * 2.0 - 1.0;

        // Use the pre-computed tangent, bitangent, and normal directly
        vec3 N = normalize(N_geom);
        vec3 T = normalize(T_geom.xyz);
        vec3 B = normalize(B_geom);
        
        // Form TBN matrix from pre-computed vectors
        mat3 TBN = mat3(T, B, N);

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
        
        // Get additional ray intersection data
        uint primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
        vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(rayQuery, true);
        uint instanceCustomIndex = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true);
        
        if (isFrontFacing) {
            // Get mesh info using the instance custom index
            MeshInfo meshInfo = u_sceneInfo.MeshInfos[instanceCustomIndex];
            
            // Get complete surface data for the hit point
            SurfaceData surface = getSurfaceDataForHit(primitiveID, barycentrics, meshInfo);
            
            vec3 worldShadingNormal = calculateShadingNormal(meshInfo, surface.texCoord, surface.normal, surface.tangent, surface.bitangent);
            vec3 albedo = sampleAlbedo(meshInfo, surface.texCoord);
    
            vec3 diffuse = DirectDiffuseLighting(albedo, worldShadingNormal, hitPosition, u_SunProperties);

            // Indirect Lighting (recursive)
            vec3 irradiance = vec3(0.0);
            // Use the ray's own direction for surface bias, not the main camera direction
            vec3 surfaceBias = DDGIGetSurfaceBias(worldShadingNormal, probeRayDirection, u_volume);

            // Get irradiance from the DDGIVolume
            irradiance = DDGIGetVolumeIrradiance(
                hitPosition,
                worldShadingNormal,
                surfaceBias,
                prevProbeIrradianceAtlas,
                prevProbeDistanceAtlas,
                u_volume);

            // Perfectly diffuse reflectors don't exist in the real world.
            // Limit the BRDF albedo to a maximum value to account for the energy loss at each bounce.
            float maxAlbedo = 0.9;

            // Store the final ray radiance and hit distance
            vec3 radiance = diffuse + ((min(albedo, vec3(maxAlbedo)) / PI) * irradiance);

            DDGIStoreProbeRayFrontfaceHit(ivec3(outputCoords), clamp(radiance, vec3(0.0), vec3(1.0)), hitT);
        } else {
            // Back face hit - store negative distance
            DDGIStoreProbeRayBackfaceHit(ivec3(outputCoords), hitT);
        }
    } else {
        // Miss - sample skybox and store blue color for visualization
        //vec3 skyboxColor = texture(u_skyboxCubemap, probeRayDirection).rgb;
        vec3 skyboxColor = vec3(0.0, 0.0, 0.0);
        // If no skybox, use green color for misses
        skyboxColor = u_SunProperties.sunColor; // Green for misses
        
        DDGIStoreProbeRayMiss(ivec3(outputCoords), skyboxColor * u_SunProperties.sunIntensity);
    }
    
}

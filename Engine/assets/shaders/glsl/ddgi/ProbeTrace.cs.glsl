#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : require

#define RAY_DATA_TEXTURE
#define DDGI_ENABLE_DIFFUSE_LIGHTING

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// Ray data output texture (set 4, binding 0)
layout(set = 4, binding = 0, rgba32f) uniform restrict writeonly image2DArray RayData;

// Previous probe data (bindless textures in set 3)
layout(set = 3, binding = 0) uniform sampler2D gTextures[];
layout(set = 3, binding = 0) uniform samplerCube gCubemaps[];
layout(set = 3, binding = 0) uniform sampler2DArray gTextureArrays[];
layout(set = 3, binding = 0) uniform usampler2DArray gUintTextureArrays[];

// TLAS binding (set 0)
layout(set = 3, binding = 2) uniform accelerationStructureEXT topLevelAS[];

// includes index and vertex buffers, the meshinfo contains the offsets into these buffers
layout(set = 3, binding = 1) readonly buffer gBindlessBuffers {
    uint data[];
} gBuffers[];

// Push constants (updated structure)
layout(push_constant) uniform PushConstants {

    uint skyboxTextureIndex;     // Index into bindless texture array
    uint lightDataSSBOIndex;
    uint lightStaticCount;
    uint lightDynamicOffset;
    uint lightDynamicCount;

    uint prevRadianceIndex;      // Previous radiance texture bindless index
    uint prevVisibilityIndex;    // Previous visibility texture bindless index


    uint tlasIndex;              // TLAS index (if using bindless TLAS)
    uint probeOffsetHandle;       // Probe offset texture bindless index
    uint probeClassificationHandle;

    float skyIntensity;
    uint volumeSlot;
} pc;

precision highp float;

// Added constant to represent an invalid index/offset (all bits set)
const uint UINT32_MAX = 0xFFFFFFFFu;

// Material header SSBO, graph pool and the generated diffuse surface graph dispatcher
#include "common/MaterialCommon.glsl"
#include "generated/SurfaceGraphsDiffuse.glsl"

// Per-instance geometry info, mirror of RtInstanceInfo (RtInstanceData.h)
struct MeshInfo {
    uint materialIndex; // bindless index into the material header SSBO

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
layout(std430, set=3, binding = 9) readonly buffer SceneInfo {
    MeshInfo MeshInfos[];
} u_sceneInfo;

// Global descriptor arrays for bindless textures (set 3)
#ifndef DESCRIPTOR_ARRAYS_DEFINED
#define DESCRIPTOR_ARRAYS_DEFINED
#endif

#include "ddgi/ProbeCommon.glsl"
#include "ddgi/IrradianceCommon.glsl"

// Input Uniforms / Buffers
layout(std140, set=0, binding = 5) uniform ProbeInfo {
    ProbeVolume volume;
} u_probeInfo[];

ProbeVolume u_volume;

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

// Interpolated hit geometry (distinct from MaterialCommon's shading SurfaceData)
struct HitSurface {
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

        // First two uint16_t may straddle a 4-byte boundary
        uint lo = gBuffers[meshInfo.iboIndex].data[indexOffset / 4];
        uint hi = gBuffers[meshInfo.iboIndex].data[indexOffset / 4 + 1];
        uint shift = (indexOffset % 4) * 8; // 0 (aligned) or 16 (misaligned)

        uint pair;
        if (shift == 0) {
            pair = lo;
        } else {
            pair = (lo >> shift) | (hi << (32 - shift));
        }
        indices.x = pair & 0xFFFF;
        indices.y = (pair >> 16) & 0xFFFF;

        // Third uint16_t at indexOffset + 4
        uint tailOff = indexOffset + 4;
        indices.z = (gBuffers[meshInfo.iboIndex].data[tailOff / 4] >> ((tailOff % 4) * 8)) & 0xFFFF;
    }
    
    return indices;
}

/**
 * Fetches vertex attributes for a single vertex
 */
VertexAttributes fetchVertexAttributes(uint vertexIndex, MeshInfo meshInfo) {
    VertexAttributes attrs;
    uint vertexOffset = vertexIndex * meshInfo.vertexStrideBytes;

    // === Position (required) ===
    vec3 position = vec3(0.0);
    if(meshInfo.positionAttributeOffsetBytes != UINT32_MAX) {
        uint posOffset = vertexOffset + meshInfo.positionAttributeOffsetBytes;
        position = vec3(
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[posOffset / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(posOffset + 4) / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(posOffset + 8) / 4])
        );
    }
    attrs.position = (meshInfo.modelMatrix * vec4(position, 1.0)).xyz;

    // === Texture coordinates (optional) ===
    if(meshInfo.texCoordAttributeOffsetBytes != UINT32_MAX) {
        uint texOffset = vertexOffset + meshInfo.texCoordAttributeOffsetBytes;
        attrs.texCoord = vec2(
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[texOffset / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(texOffset + 4) / 4])
        );
    } else {
        attrs.texCoord = vec2(0.0);
    }

    // === Normal (optional but preferred) ===
    vec3 normal = vec3(0.0, 0.0, 1.0);
    if(meshInfo.normalAttributeOffsetBytes != UINT32_MAX) {
        uint normalOffset = vertexOffset + meshInfo.normalAttributeOffsetBytes;
        normal = vec3(
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[normalOffset / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(normalOffset + 4) / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(normalOffset + 8) / 4])
        );
    }
    attrs.normal = normalize((meshInfo.modelMatrix * vec4(normal, 0.0)).xyz);

    // === Tangent (optional) - only compute if normal mapping might be used ===
    vec4 tangent = vec4(0.0);
    if(meshInfo.tangentAttributeOffsetBytes != UINT32_MAX) {
        uint tangentOffset = vertexOffset + meshInfo.tangentAttributeOffsetBytes;
        vec4 localTangent = vec4(
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[tangentOffset / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(tangentOffset + 4) / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(tangentOffset + 8) / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(tangentOffset + 12) / 4])
        );
        vec3 worldTangent = normalize((meshInfo.modelMatrix * vec4(localTangent.xyz, 0.0)).xyz);
        tangent = vec4(worldTangent, localTangent.w);
        
        // Re-orthogonalize tangent with respect to normal (in world space)
        vec3 T = normalize(tangent.xyz - dot(tangent.xyz, attrs.normal) * attrs.normal);
        attrs.tangent = vec4(T, tangent.w);

        // Calculate bitangent with proper handedness
        attrs.bitangent = normalize(cross(attrs.normal, T) * attrs.tangent.w);
    } else {
        // When no tangent data is provided, just set dummy values
        // These will be ignored if no normal mapping is used
        attrs.tangent = vec4(1.0, 0.0, 0.0, 1.0);
        attrs.bitangent = vec3(0.0, 1.0, 0.0);
    }

    return attrs;
}

/**
 * Interpolates vertex attributes using barycentric coordinates
 */
HitSurface interpolateVertexAttributes(VertexAttributes v0, VertexAttributes v1, VertexAttributes v2, vec2 barycentrics) {
    vec3 weights = vec3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    
    HitSurface surface;
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
HitSurface getSurfaceDataForHit(uint primitiveID, vec2 barycentrics, MeshInfo meshInfo) {
    // Fetch triangle indices
    uvec3 indices = fetchTriangleIndices(primitiveID, meshInfo);
    
    // Fetch vertex data for all 3 vertices
    VertexAttributes v0 = fetchVertexAttributes(indices.x, meshInfo);
    VertexAttributes v1 = fetchVertexAttributes(indices.y, meshInfo);
    VertexAttributes v2 = fetchVertexAttributes(indices.z, meshInfo);
    
    // Interpolate attributes
    return interpolateVertexAttributes(v0, v1, v2, barycentrics);
}

// Evaluates the material's compiled diffuse surface graph at a hit point
SurfaceDataDiffuse evalHitSurface(MeshInfo meshInfo, HitSurface hit) {
    MaterialData mat = getMaterialData(meshInfo.materialIndex);

    SurfaceInputs si;
    si.uv = hit.texCoord;
    si.worldPos = hit.position;
    si.worldNormal = hit.normal;
    si.tangent = hit.tangent.xyz;
    si.bitangent = hit.bitangent;
    si.flags = mat.flags;

    return evalSurfaceGraphDiffuse(mat.graphId, si, mat.graphDataOffset);
}

void main() {
    u_volume = u_probeInfo[pc.volumeSlot].volume;
    // Compute the probe index for this thread (RTXGI-style)
    int rayIndex = int(gl_LocalInvocationID.y * gl_WorkGroupSize.x + gl_LocalInvocationID.x);  // index of the ray to trace for this probe
    int probePlaneIndex = int(gl_WorkGroupID.x + u_volume.gridDimensions.x * gl_WorkGroupID.y); // index of this probe within the plane of probes
    int planeIndex = int(gl_WorkGroupID.z);                                                    // index of the plane this probe is part of
    int probesPerPlane = DDGIGetProbesPerPlane(ivec3(u_volume.gridDimensions));

    int probeIndex = (planeIndex * probesPerPlane) + probePlaneIndex;
    
    uvec3 probeTexelCoords = DDGIGetProbeTexelCoords(probeIndex, u_volume);
    uint probeState = texelFetch(gUintTextureArrays[pc.probeClassificationHandle], ivec3(probeTexelCoords), 0).r;

    const uint PROBE_STATE_ACTIVE = 0u;
    if (u_volume.probeClassificationEnabled > 0.0 && probeState != PROBE_STATE_ACTIVE && rayIndex >= int(u_volume.probeStaticRayCount)) {
        return;
    }


    // Get the probe's grid coordinates
    ivec3 probeCoords = DDGIGetProbeCoords(probeIndex, u_volume);


    vec3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, u_volume, gTextureArrays[pc.probeOffsetHandle]);
    vec3 probeRayDirection = DDGIGetProbeRayDirection(rayIndex, u_volume);
    uvec3 outputCoords = DDGIGetRayDataTexelCoords(rayIndex, probeIndex, u_volume);


    rayQueryInitializeEXT(rayQuery, topLevelAS[pc.tlasIndex], gl_RayFlagsOpaqueEXT, 0xFF, 
        probeWorldPosition, 0.0, probeRayDirection, u_volume.probeMaxRayDistance);

    // Trace the ray
    bool hit = false;
    vec3 hitPosition;
    uint hitInstanceID;
    float hitT = 1e30;
    uint primitiveID;
    vec2 barycentrics;
    uint instanceCustomIndex;
    bool isFrontFacing = true;

    rayQueryProceedEXT(rayQuery);

    // After the loop, get data from the committed intersection
    if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        hitT = rayQueryGetIntersectionTEXT(rayQuery, true);
        hitPosition = probeWorldPosition + probeRayDirection * hitT;
        hitInstanceID = rayQueryGetIntersectionInstanceIdEXT(rayQuery, true);
        isFrontFacing = rayQueryGetIntersectionFrontFaceEXT(rayQuery, true);
        
        // Get additional ray intersection data
        primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
        barycentrics = rayQueryGetIntersectionBarycentricsEXT(rayQuery, true);
        instanceCustomIndex = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true);
        hit = true;
    } else {
        hit = false;
    }

    if (hit) {
        if (isFrontFacing) {

            if (rayIndex < int(u_volume.probeStaticRayCount)) {
                DDGIStoreProbeRayFrontfaceHit(ivec3(outputCoords), vec3(0.0), hitT);
                return;
            }

            // Get mesh info using the instance custom index
            MeshInfo meshInfo = u_sceneInfo.MeshInfos[instanceCustomIndex];

            // Get interpolated hit geometry, then run the material's diffuse surface graph
            HitSurface surface = getSurfaceDataForHit(primitiveID, barycentrics, meshInfo);
            SurfaceDataDiffuse mat = evalHitSurface(meshInfo, surface);

            vec3 worldShadingNormal = normalize(mat.normal);
            vec3 albedo = mat.albedo;
            vec3 emissiveColor = mat.emission.rgb * mat.emissiveStrength;
    
            // Indirect Lighting (recursive)
            vec3 irradiance = vec3(0.0);

            // Direction from the hit point back towards the probe (for surface bias calculation)
            // Using -probeRayDirection gives us the direction from hit point to probe origin
            // This is the correct "view" direction for probe tracing (not camera position)
            vec3 hitToProbe = -probeRayDirection;

            vec3 surfaceBias = DDGIGetSurfaceBias(surface.normal, hitToProbe, u_volume);

            vec3 diffuse = DirectDiffuseLighting(albedo, surface.normal, hitPosition,
                pc.lightDataSSBOIndex, pc.lightStaticCount, pc.lightDynamicOffset, pc.lightDynamicCount,
                worldShadingNormal);

            float volumeBlendWeight = DDGIGetVolumeBlendWeight(hitPosition, u_volume);

            if (volumeBlendWeight > 0.0) {
                irradiance = DDGIGetVolumeIrradiance(
                    hitPosition,
                    surface.normal,
                    surfaceBias,
                    gTextureArrays[pc.prevRadianceIndex],
                    gTextureArrays[pc.prevVisibilityIndex],
                    gTextureArrays[pc.probeOffsetHandle],
                    gUintTextureArrays[pc.probeClassificationHandle],
                    u_volume);

                irradiance *= volumeBlendWeight;
            }

            // Perfectly diffuse reflectors don't exist in the real world.
            // Limit the BRDF albedo to a maximum value to account for the energy loss at each bounce.
            float maxAlbedo = 0.9;

            // Store the final ray radiance and hit distance
            vec3 radiance = emissiveColor + diffuse + ((min(albedo, vec3(maxAlbedo)) / PI) * irradiance);

            DDGIStoreProbeRayFrontfaceHit(ivec3(outputCoords), clamp(radiance, vec3(0.0), vec3(1.0)), hitT);
        } else {
            // Back face hit - store negative distance
            DDGIStoreProbeRayBackfaceHit(ivec3(outputCoords), hitT);
        }
    } else {
        // Miss - sample skybox and store blue color for visualization
        vec3 skyboxColor = textureLod(gCubemaps[pc.skyboxTextureIndex], probeRayDirection, 0.0).rgb;
        //skyboxColor = vec3(207.0/255.0, 236.0/255.0, 247.0/255.0);

        DDGIStoreProbeRayMiss(ivec3(outputCoords), skyboxColor * pc.skyIntensity);
    }
    
}

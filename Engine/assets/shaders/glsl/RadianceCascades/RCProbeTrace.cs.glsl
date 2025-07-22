#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : require


layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Ray data output texture (fixed binding in set 3)
layout(set = 4, binding = 0, rgba32f) uniform restrict writeonly image2DArray CascadeTextures;

precision highp float;

#include "RCCommon.glsl"


// Previous probe data (bindless textures in set 3)
layout(set = 3, binding = 0) uniform sampler2D gTextures[];
layout(set = 3, binding = 0) uniform samplerCube gCubemaps[];
layout(set = 3, binding = 0) uniform sampler2DArray gTextureArrays[];

layout(set = 3, binding = 2) uniform accelerationStructureEXT topLevelAS[];



// Bindless buffers (same as DDGI)
layout(set = 3, binding = 1) readonly buffer gBindlessBuffers {
    uint data[];
} gBuffers[];



// Push constants (updated structure)
layout(push_constant) uniform PushConstants {
    uint u_cascadeIndex;
    uint u_cascadeLevels;
    uint u_tlasIndex;
    uint u_lightCount;
    uint u_skyboxTextureIndex;
};

// Added constant to represent an invalid index/offset (all bits set)
const uint UINT32_MAX = 0xFFFFFFFFu;

// Simplified MeshInfo structure matching our C++ version (same as DDGI)
struct MeshInfo {
    uint AlbedoTextureIndex;
    uint NormalTextureIndex;
    vec3 albedo;
    vec3 emissiveColor;
    uint EmissiveFactorTextureIndex;
    
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

// Scene info buffer (reuse DDGI binding)
layout(std430, set=3, binding = 9) readonly buffer SceneInfo {
    MeshInfo MeshInfos[];
} u_sceneInfo;



layout(std140, set = 0, binding = 7) uniform CascadeLevelInfos {
    CascadeLevelInfo cascadeLevelInfo;
} cs[];

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
    uint texIndex = meshInfo.AlbedoTextureIndex;
    vec3 baseColor = meshInfo.albedo;

    if(texIndex == UINT32_MAX) {
        return baseColor; 
    }

    return baseColor * texture(gTextures[texIndex], uv).rgb;
}

vec3 sampleEmissiveColor(MeshInfo meshInfo, vec2 uv) {
    uint texIndex = meshInfo.EmissiveFactorTextureIndex;
    if(texIndex == UINT32_MAX) {
        return meshInfo.emissiveColor;
    }
    return texture(gTextures[texIndex], uv).rgb * meshInfo.emissiveColor;
}



vec3 SphericalFibonacci(uint sampleIndex, uint numSamples)
{
    const float b = (sqrt(5.0) * 0.5 + 0.5) - 1.0;
    float phi = 6.28318530718 * fract(float(sampleIndex) * b);
    float cosTheta = 1.0 - (2.0 * float(sampleIndex) + 1.0) * (1.0 / float(numSamples));
    float sinTheta = sqrt(clamp(1.0 - (cosTheta * cosTheta), 0.0, 1.0));

    return vec3((cos(phi) * sinTheta), (sin(phi) * sinTheta), cosTheta);
}

vec3 GetProbeRayDirection(uint rayIndex, CascadeLevelInfo cascadeLevelInfo)
{
    uint sampleIndex = rayIndex;
    uint numRays = cascadeLevelInfo.angularResolution * cascadeLevelInfo.angularResolution;
    
    // Get a deterministic direction on the sphere using the spherical Fibonacci sequence
    vec3 direction = SphericalFibonacci(sampleIndex, numRays);

    return normalize(direction);

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

/**
 * Computes the visibility factor for a given vector to a light using ray tracing.
 */
float LightVisibility(
    vec3 worldPosition,
    vec3 normal,
    vec3 lightVector,
    float tmax,
    float normalBias,
    float viewBias)
{
    // Create a visibility ray query
    rayQueryEXT visibilityQuery;
    
    vec3 rayOrigin = worldPosition + (normal * normalBias); 
    vec3 rayDirection = normalize(lightVector);
    
    // Initialize ray query for visibility test
    // Use ACCEPT_FIRST_HIT and SKIP_CLOSEST_HIT equivalent flags
    rayQueryInitializeEXT(
        visibilityQuery, 
        topLevelAS[0], 
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 
        0xFF,
        rayOrigin, 
        0.0, 
        rayDirection, 
        tmax
    );
    
    // Process the ray query
    rayQueryProceedEXT(visibilityQuery);
    
    // Check if we hit anything (if we did, the light is occluded)
    if (rayQueryGetIntersectionTypeEXT(visibilityQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        return 0.0; // Occluded
    }
    
    return 1.0; // Visible
}

vec3 EvaluateDirectionalLight(vec3 shadingNormal, vec3 hitPositionWorld, LightInfo light) {   
    
    // Use ray-traced visibility instead of shadow maps
    float normalBias = 0.01; // Small bias to avoid self-intersection
    float viewBias = 0.0;    // Not used yet, but kept for future
    
    float visibility = LightVisibility(
        hitPositionWorld,
        shadingNormal,
        -light.direction.xyz,  // Light vector (towards sun)
        1e27,                              // Very large tmax for directional light
        normalBias,
        viewBias
    );
    
    // Early out if the light isn't visible from the surface
    if (visibility <= 0.0) {
        return vec3(0.0);
    }
    
    // Compute lighting
    //vec3 lightDirection = normalize(-light.direction.xyz);
    
    return light.color.xyz * light.color.w * visibility;
}

vec3 EvaluateSpotLight(vec3 shadingNormal, vec3 hitPositionWorld, LightInfo spotLight)
{   
    return vec3(0.0);
}

vec3 EvaluatePointLight(vec3 shadingNormal, vec3 hitPositionWorld, LightInfo pointLight)
{   
    return vec3(0.0);
}

// calculate the radiance
vec3 DirectDiffuseLighting(vec3 albedo, vec3 shadingNormal, vec3 hitPositionWorld, uint lightCount) {


    vec3 totalLighting = vec3(0.0);

    for (uint i = 0; i < lightCount; ++i) {
        LightInfo light = u_lights[i].light;
        
        // light type is in light.position.w
        if (light.position.w == 1) { // Directional
             totalLighting += EvaluateDirectionalLight(shadingNormal, hitPositionWorld, light);
        } else if (light.position.w == 0) { // Point
             totalLighting += EvaluatePointLight(shadingNormal, hitPositionWorld, light);
        } else if (light.position.w == 2) { // Spot
             totalLighting += EvaluateSpotLight(shadingNormal, hitPositionWorld, light);
        }
    }


    return (albedo * totalLighting);
}

vec3 calculateShadingNormal(
    MeshInfo meshInfo,
    vec2 uv,
    vec3 N_geom,   // Interpolated geometric normal (world space)
    vec4 T_geom,   // Interpolated tangent (world space, w component contains handedness)
    vec3 B_geom    // Pre-computed bitangent (world space)
) {
    // If no normal map is provided, just return the geometric normal
    // This avoids issues with inconsistent tangent generation
    if (meshInfo.NormalTextureIndex == UINT32_MAX) {
        return normalize(N_geom);
    }

    // Apply normal map only if a valid texture index is provided
    vec3 tangentNormal = texture(gTextures[meshInfo.NormalTextureIndex], uv).xyz;
    tangentNormal = tangentNormal * 2.0 - 1.0;

    // Use the pre-computed tangent, bitangent, and normal directly
    vec3 N = normalize(N_geom);
    vec3 T = normalize(T_geom.xyz);
    vec3 B = normalize(B_geom);

    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

ivec3 getProbeCoords(uvec3 outputCoords, uint angularResolution) {
    ivec3 probeCoords;
    probeCoords.x = int(outputCoords.x / angularResolution);
    probeCoords.z = int(outputCoords.y / angularResolution);
    probeCoords.y = int(outputCoords.z);

    return probeCoords;
}

uint getRayIndex(uvec3 outputCoords, uint angularResolution) {
    uint rayIndexX = outputCoords.x % angularResolution;
    uint rayIndexY = outputCoords.y % angularResolution;
    uint rayIndex = rayIndexY * angularResolution + rayIndexX;

    return rayIndex;
}


void main() {
    CascadeLevelInfo cascadeLevelInfo = cs[u_cascadeIndex].cascadeLevelInfo;
    uint angularResolution = cascadeLevelInfo.angularResolution;
    
    uvec3 outputCoords = uvec3(gl_GlobalInvocationID.xyz);
    
    // The size of the output texture array
    uvec3 imageSize = uvec3(imageSize(CascadeTextures));

    // Discard threads that are outside of the image dimensions.
    // This can happen due to workgroup rounding.
    if (outputCoords.x >= imageSize.x || 
        outputCoords.y >= imageSize.y || 
        outputCoords.z >= imageSize.z) {
        return;
    }

    // From the output texel coordinates, we can derive the probe and ray indices.
    ivec3 probeCoords = getProbeCoords(outputCoords, angularResolution);
    uint rayIndex = getRayIndex(outputCoords, angularResolution);


    vec3 probeWorldPosition = GetProbeWorldPosition(probeCoords, cascadeLevelInfo);
    vec3 probeRayDirection = GetProbeRayDirection(rayIndex, cascadeLevelInfo);
    
    rayQueryEXT query;
    rayQueryInitializeEXT(
        query,
        topLevelAS[u_tlasIndex],
        gl_RayFlagsOpaqueEXT,
        0xFF,
        probeWorldPosition,
        cascadeLevelInfo.minProbeDistance,
        probeRayDirection,
        cascadeLevelInfo.maxProbeDistance
    );

    // Proceed with the ray query. The loop continues until the query is complete.
    // With gl_RayFlagsOpaqueEXT, the hardware automatically finds the closest hit,
    // so we just need to let the query run to completion.

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

    // Default to a miss. The hit distance is set to the max distance for the cascade.
    // The color is black, but it will be overwritten by the skybox for the last cascade,
    // or by the previous cascade's data during the merge step.
    vec4 outColor = vec4(0.0, 0.0, 0.0, 0.0);


    if (hit) {
        if (isFrontFacing) {
            // Get additional ray intersection data
            uint primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
            vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(query, true);
            uint instanceCustomIndex = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);
            vec3 hitPosition = probeWorldPosition + probeRayDirection * hitT;

            // Get mesh info using the instance custom index
            MeshInfo meshInfo = u_sceneInfo.MeshInfos[instanceCustomIndex];
            
            // Get surface data for the hit point
            SurfaceData surface = getSurfaceDataForHit(primitiveID, barycentrics, meshInfo);
            
            vec3 worldShadingNormal = calculateShadingNormal(meshInfo, surface.texCoord, surface.normal, surface.tangent, surface.bitangent);
            vec3 albedo = sampleAlbedo(meshInfo, surface.texCoord);
            vec3 emissiveColor = sampleEmissiveColor(meshInfo, surface.texCoord);

            vec3 diffuse = DirectDiffuseLighting(albedo, worldShadingNormal, hitPosition, u_lightCount);

            vec3 radiance = diffuse + emissiveColor;

            outColor = vec4(radiance, 1.0);
        } else {
            
            //outColor = vec4(0.0, 0.0, 0.0, 0.0); // Red for back-face hit
        }
    } else {

        if (u_cascadeIndex == u_cascadeLevels - 1) {
            if (u_skyboxTextureIndex != UINT32_MAX) {
                vec3 skyboxColor = texture(gCubemaps[u_skyboxTextureIndex], probeRayDirection).rgb;
                outColor.rgb = skyboxColor;
                outColor.a = 1.0; // A miss is a miss. Alpha must be 0 for the merge to work.
            }
        }

        

    }


    // Store the resulting color.
    imageStore(CascadeTextures, ivec3(outputCoords), outColor);
}

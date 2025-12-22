#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : require

#include "RCCommon.glsl"


layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 4, binding = 0, rgba32f) uniform restrict writeonly image2DArray RadianceOutput[RC_MAX_CASCADES];


layout(std140, set = 4, binding = 1) uniform VolumeInfo {
    RCVolume u_volume;
};

// Common lighting structures and functions
#ifdef PI
#else
    #define PI 3.14159265359
#endif

struct SunProperties {
    vec4 position;      // w = light type (0 = point, 1 = directional, 2 = spot)
    vec4 direction;     // w = range
    vec4 color;         // w = intensity
    vec4 spotAngles;    // x = inner cone cos, y = outer cone cos, z = entity id, w = unused
};

// Assuming light data is in set 0 as per engine convention
layout(std140, set=0, binding = 1) uniform SunPropertiesUBO {
    SunProperties sunProperties;
} u_sunProperties[];



layout(set = 3, binding = 0) uniform sampler2D gTextures[];
layout(set = 3, binding = 0) uniform samplerCube gCubemaps[];
layout(set = 3, binding = 0) uniform sampler2DArray gTextureArrays[];

layout(set = 3, binding = 2) uniform accelerationStructureEXT topLevelAS[];

layout(set = 3, binding = 1) readonly buffer gBindlessBuffers {
    uint data[];
} gBuffers[];

layout(push_constant) uniform PushConstants {
    uint tlasIndex;
    uint skyboxTextureIndex;
    uint cascadeIndex;
    uint lightCount;
} pc;

const uint UINT32_MAX = 0xFFFFFFFFu;

struct MeshInfo {
    uint AlbedoTextureIndex;
    uint NormalTextureIndex;
    vec3 albedo;
    vec3 emissiveColor;
    uint EmissiveFactorTextureIndex;

    uint iboIndex;
    uint vboIndex;
    uint meshIndex;

    mat4 modelMatrix;

    uint positionAttributeOffsetBytes;
    uint texCoordAttributeOffsetBytes;
    uint normalAttributeOffsetBytes;
    uint tangentAttributeOffsetBytes;

    uint vertexStrideBytes;
    uint indexType;
};

layout(std430, set=3, binding = 9) readonly buffer SceneInfo {
    MeshInfo MeshInfos[];
} u_sceneInfo;

struct VertexAttributes {
    vec3 position;
    vec2 texCoord;
    vec3 normal;
};

uvec3 fetchTriangleIndices(uint primitiveID, MeshInfo meshInfo) {
    uint baseIndex = primitiveID * 3;
    uvec3 indices;

    if (meshInfo.indexType == 5125) {
        uint indexOffset = baseIndex * 4;
        indices.x = gBuffers[meshInfo.iboIndex].data[indexOffset / 4];
        indices.y = gBuffers[meshInfo.iboIndex].data[(indexOffset + 4) / 4];
        indices.z = gBuffers[meshInfo.iboIndex].data[(indexOffset + 8) / 4];
    } else {
        uint indexOffset = baseIndex * 2;
        uint packedIndices0 = gBuffers[meshInfo.iboIndex].data[indexOffset / 4];
        uint packedIndices1 = gBuffers[meshInfo.iboIndex].data[(indexOffset + 4) / 4];

        indices.x = packedIndices0 & 0xFFFF;
        indices.y = (packedIndices0 >> 16) & 0xFFFF;
        indices.z = packedIndices1 & 0xFFFF;
    }

    return indices;
}

VertexAttributes fetchVertexAttributes(uint vertexIndex, MeshInfo meshInfo) {
    VertexAttributes attrs;
    uint vertexOffset = vertexIndex * meshInfo.vertexStrideBytes;

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

    if(meshInfo.texCoordAttributeOffsetBytes != UINT32_MAX) {
        uint texOffset = vertexOffset + meshInfo.texCoordAttributeOffsetBytes;
        attrs.texCoord = vec2(
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[texOffset / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(texOffset + 4) / 4])
        );
    } else {
        attrs.texCoord = vec2(0.0);
    }

    vec3 normal = vec3(0.0, 1.0, 0.0);
    if(meshInfo.normalAttributeOffsetBytes != UINT32_MAX) {
        uint normalOffset = vertexOffset + meshInfo.normalAttributeOffsetBytes;
        normal = vec3(
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[normalOffset / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(normalOffset + 4) / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(normalOffset + 8) / 4])
        );
    }
    attrs.normal = normalize((meshInfo.modelMatrix * vec4(normal, 0.0)).xyz);

    return attrs;
}

struct SurfaceData {
    vec3 position;
    vec2 texCoord;
    vec3 normal;
};

SurfaceData getSurfaceDataForHit(uint primitiveID, vec2 barycentrics, MeshInfo meshInfo) {
    uvec3 indices = fetchTriangleIndices(primitiveID, meshInfo);

    VertexAttributes v0 = fetchVertexAttributes(indices.x, meshInfo);
    VertexAttributes v1 = fetchVertexAttributes(indices.y, meshInfo);
    VertexAttributes v2 = fetchVertexAttributes(indices.z, meshInfo);

    vec3 weights = vec3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);

    SurfaceData surface;
    surface.position = v0.position * weights.x + v1.position * weights.y + v2.position * weights.z;
    surface.texCoord = v0.texCoord * weights.x + v1.texCoord * weights.y + v2.texCoord * weights.z;
    surface.normal = normalize(v0.normal * weights.x + v1.normal * weights.y + v2.normal * weights.z);

    return surface;
}

vec3 sampleAlbedo(MeshInfo meshInfo, vec2 uv) {
    uint texIndex = meshInfo.AlbedoTextureIndex;
    vec3 baseColor = meshInfo.albedo;

    if(texIndex == UINT32_MAX) {
        return baseColor;
    }

    return baseColor * texture(gTextures[texIndex], uv).rgb;
}

vec3 sampleEmissive(MeshInfo meshInfo, vec2 uv) {
    uint texIndex = meshInfo.EmissiveFactorTextureIndex;
    if(texIndex == UINT32_MAX) {
        return meshInfo.emissiveColor;
    }
    return texture(gTextures[texIndex], uv).rgb * meshInfo.emissiveColor;
}

// shadow and lighting functions
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
    
    vec3 rayOrigin = worldPosition + (normal * normalBias); // TODO: not using viewBias yet
    vec3 rayDirection = normalize(lightVector);
    
    // Initialize ray query for visibility test
    // Use ACCEPT_FIRST_HIT and SKIP_CLOSEST_HIT equivalent flags
    rayQueryInitializeEXT(
        visibilityQuery, 
        topLevelAS[pc.tlasIndex], 
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

float LightFalloff(float distanceToLight) {
    return 1.0 / pow(max(distanceToLight, 1.0), 2);
}

float LightWindowing(float distanceToLight, float maxDistance) {
    return pow(clamp(1.0 - pow((distanceToLight / maxDistance), 4), 0.0, 1.0), 2);
}

/**
 * Evaluate direct lighting for the current surface and the directional light using ray-traced visibility.
 */
vec3 EvaluateDirectionalLight(vec3 shadingNormal, vec3 hitPositionWorld, SunProperties sunProperties) {   
    
    // Use ray-traced visibility instead of shadow maps
    float normalBias = 0.01; // Small bias to avoid self-intersection
    float viewBias = 0.0;    // Not used yet, but kept for future
    
    float visibility = LightVisibility(
        hitPositionWorld,
        shadingNormal,
        -sunProperties.direction.xyz,  // Light vector (towards sun)
        1e27,                              // Very large tmax for directional light
        normalBias,
        viewBias
    );
    
    // Early out if the light isn't visible from the surface
    if (visibility <= 0.0) {
        return vec3(0.0);
    }
    
    // Compute lighting
    vec3 lightDirection = normalize(-sunProperties.direction.xyz);
    float NdotL = max(dot(shadingNormal, lightDirection), 0.0);
    
    return sunProperties.color.xyz * sunProperties.color.w * NdotL * visibility;
}

vec3 EvaluateSpotLight(vec3 shadingNormal, vec3 hitPositionWorld, SunProperties sunProperties)
{   
    return vec3(0.0);
}

vec3 EvaluatePointLight(vec3 shadingNormal, vec3 hitPositionWorld, SunProperties PointLight)
{   
    float normalBias = 0.01; // Small bias to avoid self-intersection
    float viewBias = 0.0;    // Not used yet, but kept for future

    vec3 lightVector = (PointLight.position.xyz - hitPositionWorld);
    float  lightDistance = length(lightVector);

    // Early out, light energy doesn't reach the surface
    if (lightDistance > PointLight.direction.w) return vec3(0.0); // direction.w is range for point light

    float tmax = (lightDistance - viewBias);
    float visibility = LightVisibility(hitPositionWorld, shadingNormal, lightVector, tmax, normalBias, viewBias);

    // Early out, this light isn't visible from the surface
    if (visibility <= 0.0) return vec3(0.0);

    // Compute lighting
    vec3 lightDirection = normalize(lightVector);
    float  nol = max(dot(shadingNormal, lightDirection), 0.0);
    float  falloff = LightFalloff(lightDistance);
    float  window = LightWindowing(lightDistance, PointLight.direction.w); // direction.w is range for point light

    vec3 color = PointLight.color.xyz * PointLight.color.w * nol * falloff * window * visibility;

    return color;
}

/**
 * Computes the diffuse reflection of light off the given surface (direct lighting).
 */
vec3 DirectDiffuseLighting(vec3 albedo, vec3 shadingNormal, vec3 hitPositionWorld, uint lightCount) {

    vec3 brdf = (albedo / PI);

    vec3 totalLighting = vec3(0.0);

    for (uint i = 0; i < lightCount; ++i) {
        SunProperties light = u_sunProperties[i].sunProperties;
        
        // light type is in light.position.w
        if (light.position.w == 1) { // Directional
             totalLighting += EvaluateDirectionalLight(shadingNormal, hitPositionWorld, light);
        } else if (light.position.w == 0) { // Point
             totalLighting += EvaluatePointLight(shadingNormal, hitPositionWorld, light);
        } else if (light.position.w == 2) { // Spot
             totalLighting += EvaluateSpotLight(shadingNormal, hitPositionWorld, light);
        }
    }

    return (brdf * totalLighting);
}

rayQueryEXT rayQuery;

void main() {
    RCCascade cascade = u_volume.cascades[pc.cascadeIndex];

    uint angRes = cascade.angularResolution;
    uint tilesPerDim = (angRes + 7) / 8;

    ivec3 probeCoords;
    probeCoords.x = int(gl_WorkGroupID.x / tilesPerDim);
    probeCoords.z = int(gl_WorkGroupID.y / tilesPerDim);
    probeCoords.y = int(gl_WorkGroupID.z / angRes);

    uint tileX = gl_WorkGroupID.x % tilesPerDim;
    uint tileY = gl_WorkGroupID.y % tilesPerDim;
    uint rayZ = gl_WorkGroupID.z % angRes;

    uint rayX = tileX * 8 + gl_LocalInvocationID.x;
    uint rayY = tileY * 8 + gl_LocalInvocationID.y;

    if (rayX >= angRes || rayY >= angRes) {
        return;
    }

    int rayIndex = int(rayZ * angRes * angRes + rayY * angRes + rayX);

    vec3 probeWorldPosition = RCGetProbeWorldPosition(probeCoords, cascade);
    vec3 rayDirection = RCGetRayDirection(rayIndex, cascade);
    uvec3 outputCoords = RCGetRayDataTexelCoords(rayIndex, probeCoords, cascade);

    float tMin = cascade.minRange;
    float tMax = cascade.maxRange;

    rayQueryInitializeEXT(
        rayQuery,
        topLevelAS[pc.tlasIndex],
        gl_RayFlagsOpaqueEXT,
        0xFF,
        probeWorldPosition,
        tMin,
        rayDirection,
        tMax
    );

    bool hit = false;
    float hitT = tMax;
    uint primitiveID;
    vec2 barycentrics;
    uint instanceCustomIndex;
    bool isFrontFacing = true;

    while (rayQueryProceedEXT(rayQuery)) {
        if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            float t = rayQueryGetIntersectionTEXT(rayQuery, false);
            if (t < hitT) {
                hitT = t;
                hit = true;
                rayQueryConfirmIntersectionEXT(rayQuery);
            }
        }
    }

    if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        hitT = rayQueryGetIntersectionTEXT(rayQuery, true);
        isFrontFacing = rayQueryGetIntersectionFrontFaceEXT(rayQuery, true);
        primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
        barycentrics = rayQueryGetIntersectionBarycentricsEXT(rayQuery, true);
        instanceCustomIndex = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true);
        hit = true;
    } else {
        hit = false;
    }

    vec4 result;

    if (hit) {
        if (isFrontFacing) {
            MeshInfo meshInfo = u_sceneInfo.MeshInfos[instanceCustomIndex];
            SurfaceData surface = getSurfaceDataForHit(primitiveID, barycentrics, meshInfo);

            vec3 albedo = sampleAlbedo(meshInfo, surface.texCoord);
            vec3 emissive = sampleEmissive(meshInfo, surface.texCoord);
            
            vec3 directLighting = DirectDiffuseLighting(albedo, surface.normal, surface.position, pc.lightCount);
            vec3 radiance = emissive + directLighting;

            result = vec4(radiance, hitT);
        } else {
            result = vec4(0.0, 0.0, 0.0, -hitT);
        }
    } else {
        vec3 skyColor = texture(gCubemaps[pc.skyboxTextureIndex], rayDirection).rgb;
        result = vec4(skyColor, tMax * 10.0);
    }

    imageStore(RadianceOutput[pc.cascadeIndex], ivec3(outputCoords), result);
}

#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "common/MaterialCommon.glsl"
#include "generated/SurfaceGraphs.glsl"
#include "terrain/terrain_common.glsl"

layout(location = 0) out vec2 gNormal;
layout(location = 1) out vec4 gAlbedoSpec;
layout(location = 2) out vec4 gMaterial;

layout(location = 0) in vec4 inFragPosDepth;
layout(location = 3) in flat uint inChunkIndex;
layout(location = 4) in flat uint inLOD;
layout(location = 5) in float inNormalizedHeight;

layout(push_constant) uniform TerrainPushConstants {
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint chunkDataBufferIndex;
    uint continentalnessIndex; // Also used for single heightmap when useMultiNoise = 0
    uint erosionIndex;
    uint peaksValleysIndex;
    uint splineCurveIndex;
    uint useMultiNoise;
    uint lodResolution;
    float heightScale;
    float terrainWorldSize;
    uint grassMaterialIndex;
    uint rockMaterialIndex;
    uint snowMaterialIndex;
} pc;

float getHeightTexelWorldSize() {
    ivec2 texSize = textureSize(u_textures[pc.continentalnessIndex], 0);
    return pc.terrainWorldSize / float(texSize.x);
}

float sampleHeightWorld(vec2 worldXZ) {
    float raw = pc.useMultiNoise > 0u
        ? sampleHeightRaw_CEPV(worldXZ, pc.terrainWorldSize, u_textures[pc.continentalnessIndex], u_textures[pc.erosionIndex], u_textures[pc.peaksValleysIndex], u_textures[pc.splineCurveIndex])
        : sampleHeightRaw_Single(worldXZ, pc.terrainWorldSize, u_textures[pc.continentalnessIndex]);
    return rawToWorldHeight(raw, pc.heightScale);
}

vec3 computeTerrainNormal(vec3 worldPos) {
    float step = getHeightTexelWorldSize();

    float hL = sampleHeightWorld(worldPos.xz + vec2(-step, 0.0));
    float hR = sampleHeightWorld(worldPos.xz + vec2( step, 0.0));
    float hD = sampleHeightWorld(worldPos.xz + vec2(0.0, -step));
    float hU = sampleHeightWorld(worldPos.xz + vec2(0.0,  step));

    float dhdx = (hR - hL) / (2.0 * step);
    float dhdz = (hU - hD) / (2.0 * step);

    vec3 dx = vec3(1.0, dhdx, 0.0);
    vec3 dz = vec3(0.0, dhdz, 1.0);

    return normalize(cross(dz, dx));
}

vec2 triplanarUV(vec3 worldPos, vec3 normal, float scale) {
    vec3 n = abs(normal);
    if (n.y >= n.x && n.y >= n.z) {
        return worldPos.xz * scale;
    } else if (n.x >= n.z) {
        return worldPos.yz * scale;
    } else {
        return worldPos.xy * scale;
    }
}

struct TerrainSample {
    vec3 albedo;
    float roughness;
    float metallic;
    float ao;
};

TerrainSample sampleTerrainMaterial(uint matIndex, vec3 worldPos, vec3 normal) {
    MaterialData mat = getMaterialData(matIndex);
    float scale = mat.tilingScale > 0.0 ? mat.tilingScale : 1.0;
    vec2 uv = triplanarUV(worldPos, normal, scale);

    // Temporary bridge to the unified graph model until the terrain path is reworked
    SurfaceInputs si;
    si.uv = uv;
    si.worldPos = worldPos;
    si.worldNormal = normal;
    si.tangent = vec3(1.0, 0.0, 0.0);
    si.bitangent = vec3(0.0, 0.0, 1.0);
    si.flags = mat.flags;

    SurfaceData surf = evalSurfaceGraph(mat.graphId, si, mat.graphDataOffset);

    TerrainSample s;
    s.albedo = surf.albedo;
    s.roughness = surf.roughness;
    s.metallic = surf.metallic;
    s.ao = surf.ao;
    return s;
}

TerrainSample blendSamples(TerrainSample a, TerrainSample b, float t) {
    TerrainSample result;
    result.albedo = mix(a.albedo, b.albedo, t);
    result.roughness = mix(a.roughness, b.roughness, t);
    result.metallic = mix(a.metallic, b.metallic, t);
    result.ao = mix(a.ao, b.ao, t);
    return result;
}

void main() {
    vec3 normalWS = computeTerrainNormal(inFragPosDepth.xyz);
    gNormal = octEncodeNormal(normalize(normalWS));

    MaterialData grassMat = getMaterialData(pc.grassMaterialIndex);
    float slopeThreshold = grassMat.slopeThreshold > 0.0 ? grassMat.slopeThreshold : 0.7;
    float heightBlend = grassMat.heightBlend > 0.0 ? grassMat.heightBlend : 0.8;

    float slope = 1.0 - normalWS.y;
    float height = inNormalizedHeight;

    TerrainSample grass = sampleTerrainMaterial(pc.grassMaterialIndex, inFragPosDepth.xyz, normalWS);
    TerrainSample rock = sampleTerrainMaterial(pc.rockMaterialIndex, inFragPosDepth.xyz, normalWS);
    TerrainSample snow = sampleTerrainMaterial(pc.snowMaterialIndex, inFragPosDepth.xyz, normalWS);

    float slopeBlend = smoothstep(slopeThreshold - 0.1, slopeThreshold + 0.1, slope);
    TerrainSample groundLayer = blendSamples(grass, rock, slopeBlend);

    float snowBlend = smoothstep(heightBlend - 0.1, heightBlend + 0.1, height) * (1.0 - slope * 0.5);
    TerrainSample finalSample = blendSamples(groundLayer, snow, snowBlend);

    //vec3 finalCol = vec3(1.0);
    //if (height < 0.4) {
    //    finalCol = vec3(0.0, 0.0, 1.0);
    //} else if (height < 0.5) {
    //    finalCol = vec3(0.0, 1.0, 0.0);
    //} else if (height <= 0.6) {
    //    finalCol = vec3(0.1, 0.1, 0.1);
    //} else {
    //    finalCol = vec3(0.9, 0.9, 0.9);
    //}




    gAlbedoSpec = vec4(finalSample.albedo, 1.0);
    gMaterial = vec4(finalSample.metallic, finalSample.roughness, finalSample.ao, packShadingModel(SM_OPENPBR_STANDARD));
}

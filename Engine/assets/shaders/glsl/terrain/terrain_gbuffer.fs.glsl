#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "common/MaterialCommon.glsl"

layout(location = 0) out vec4 gPositionDepth;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedoSpec;
layout(location = 3) out vec4 gMaterial;

layout(location = 0) in vec4 inFragPosDepth;
layout(location = 3) in flat uint inChunkIndex;
layout(location = 4) in flat uint inLOD;
layout(location = 5) in float inNormalizedHeight;

layout(set = 1, binding = 0) uniform MaterialDataBuffer {
    MaterialData data;
} u_materials[];

layout(set = 3, binding = 0) uniform sampler2D u_textures[];
layout(set = 3, binding = 0) uniform sampler3D u_textures3D[];

layout(push_constant) uniform TerrainPushConstants {
    uint cameraBindlessIndex;
    uint chunkDataBufferIndex;
    uint continentalnessIndex;
    uint erosionIndex;
    uint peaksValleysIndex;
    uint noiseLUTIndex;
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
    vec2 uv = worldXZ / pc.terrainWorldSize + 0.5;

    float c = texture(u_textures[pc.continentalnessIndex], uv).r * 2.0 - 1.0;
    float e = texture(u_textures[pc.erosionIndex], uv).r * 2.0 - 1.0;
    float pv = texture(u_textures[pc.peaksValleysIndex], uv).r * 2.0 - 1.0;

    vec3 lutCoord = vec3(c, e, pv) * 0.5 + 0.5;
    float raw = texture(u_textures3D[pc.noiseLUTIndex], lutCoord).r;

    return (raw - 0.5) * pc.heightScale;
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
    MaterialData mat = u_materials[matIndex].data;
    float scale = mat.tilingScale > 0.0 ? mat.tilingScale : 1.0;
    vec2 uv = triplanarUV(worldPos, normal, scale);

    TerrainSample s;
    s.albedo = SAMPLE_ALBEDO(mat, u_textures, uv);
    s.roughness = SAMPLE_ROUGHNESS(mat, u_textures, uv);
    s.metallic = SAMPLE_METALLIC(mat, u_textures, uv);
    s.ao = SAMPLE_AO(mat, u_textures, uv);
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
    gPositionDepth = inFragPosDepth;

    vec3 normalWS = computeTerrainNormal(inFragPosDepth.xyz);
    gNormal = vec4(normalWS, 1.0);

    MaterialData grassMat = u_materials[pc.grassMaterialIndex].data;
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

    gAlbedoSpec = vec4(finalSample.albedo, 1.0);
    gMaterial = vec4(finalSample.metallic, finalSample.roughness, finalSample.ao, 1.0);
}

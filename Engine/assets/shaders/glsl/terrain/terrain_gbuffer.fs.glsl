#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "common/MaterialCommon.glsl"
#include "generated/TerrainGraphs.glsl"
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
    uint materialIndex;
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

struct TerrainGeometry {
    vec3 normal;
    float curvature;
};

// Normal and curvature share the same four height taps: the normal is their central difference and
// the curvature their laplacian against the centre height, so curvature costs no extra samples.
TerrainGeometry computeTerrainGeometry(vec3 worldPos) {
    float step = getHeightTexelWorldSize();

    float hL = sampleHeightWorld(worldPos.xz + vec2(-step, 0.0));
    float hR = sampleHeightWorld(worldPos.xz + vec2( step, 0.0));
    float hD = sampleHeightWorld(worldPos.xz + vec2(0.0, -step));
    float hU = sampleHeightWorld(worldPos.xz + vec2(0.0,  step));

    float dhdx = (hR - hL) / (2.0 * step);
    float dhdz = (hU - hD) / (2.0 * step);

    vec3 dx = vec3(1.0, dhdx, 0.0);
    vec3 dz = vec3(0.0, dhdz, 1.0);

    TerrainGeometry geom;
    geom.normal = normalize(cross(dz, dx));
    geom.curvature = ((hL + hR + hD + hU) * 0.25 - worldPos.y) / step;
    return geom;
}

void main() {
    vec3 worldPos = inFragPosDepth.xyz;
    TerrainGeometry geom = computeTerrainGeometry(worldPos);

    MaterialData mat = getMaterialData(pc.materialIndex);

    TerrainInputs si;
    si.uv = worldPos.xz / pc.terrainWorldSize + 0.5;
    si.worldPos = worldPos;
    si.worldNormal = geom.normal;
    si.normalizedHeight = inNormalizedHeight;
    si.curvature = geom.curvature;
    si.lod = inLOD;
    si.continentalnessTex = pc.continentalnessIndex;
    si.erosionTex = pc.erosionIndex;
    si.peaksValleysTex = pc.peaksValleysIndex;

    TerrainSurfaceData surf = evalTerrainSurfaceGraph(mat.graphId, si, mat.graphDataOffset);

    gNormal = octEncodeNormal(normalize(surf.normal));
    gAlbedoSpec = vec4(surf.albedo, 1.0);
    gMaterial = vec4(surf.metallic, surf.roughness, surf.ao, packShadingModel(surf.shadingModelId));
}

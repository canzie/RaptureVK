#version 460

#extension GL_EXT_nonuniform_qualifier : require

// GBuffer outputs
layout(location = 0) out vec4 gPositionDepth;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedoSpec;
layout(location = 3) out vec4 gMaterial;

// Inputs from vertex shader
layout(location = 0) in vec4 inFragPosDepth;
layout(location = 3) in flat uint inChunkIndex;
layout(location = 4) in flat uint inLOD;
layout(location = 5) in float inNormalizedHeight;



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
} pc;

// Height-based color gradient (cold to warm)
vec3 heightToColor(float t) {
    // Cold (blue) to warm (orange/red) gradient
    vec3 cold = vec3(0.1, 0.3, 0.6);      // Deep blue
    vec3 mid1 = vec3(0.2, 0.6, 0.4);      // Teal/green
    vec3 mid2 = vec3(0.6, 0.7, 0.3);      // Yellow-green
    vec3 warm = vec3(0.8, 0.4, 0.2);      // Orange-red

    if (t < 0.33) {
        return mix(cold, mid1, t * 3.0);
    } else if (t < 0.66) {
        return mix(mid1, mid2, (t - 0.33) * 3.0);
    } else {
        return mix(mid2, warm, (t - 0.66) * 3.0);
    }
}


vec3 checker2D(vec2 p, float cellSize, vec3 a, vec3 b)
{
    ivec2 cell = ivec2(floor(p / cellSize));
    int parity = (cell.x ^ cell.y) & 1;
    return parity == 0 ? a : b;
}

vec3 triplanarChecker(vec3 worldPos, vec3 normal, float cellSize, vec3 colorA, vec3 colorB)
{
    vec3 n = abs(normalize(normal));

    // Pick dominant axis - blending discrete checker colors causes gray artifacts
    if (n.y >= n.x && n.y >= n.z) {
        return checker2D(worldPos.xz, cellSize, colorA, colorB);
    } else if (n.x >= n.z) {
        return checker2D(worldPos.yz, cellSize, colorA, colorB);
    } else {
        return checker2D(worldPos.xy, cellSize, colorA, colorB);
    }
}


float getHeightTexelWorldSize()
{
    ivec2 texSize = textureSize(u_textures[pc.continentalnessIndex], 0);
    return pc.terrainWorldSize / float(texSize.x);
}

float sampleHeightWorld(vec2 worldXZ)
{
    vec2 uv = worldXZ / pc.terrainWorldSize + 0.5;

    float c = texture(u_textures[pc.continentalnessIndex], uv).r * 2.0 - 1.0;
    float e = texture(u_textures[pc.erosionIndex], uv).r * 2.0 - 1.0;
    float pv = texture(u_textures[pc.peaksValleysIndex], uv).r * 2.0 - 1.0;

    vec3 lutCoord = vec3(c, e, pv) * 0.5 + 0.5;
    float raw = texture(u_textures3D[pc.noiseLUTIndex], lutCoord).r;

    return (raw - 0.5) * pc.heightScale;
}

vec3 computeTerrainNormal(vec3 worldPos)
{
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


void main() {
    gPositionDepth = inFragPosDepth;


    vec3 normalWS = computeTerrainNormal(inFragPosDepth.xyz);


    gNormal = vec4(normalWS, 1.0);

    //vec3 albedo = heightToColor(clamp(inNormalizedHeight, 0.0, 1.0));
    vec3 albedo = triplanarChecker(inFragPosDepth.xyz, normalWS, 2.0, vec3(0.1, 0.1, 0.1), vec3(0.9, 0.9, 0.9));

    gAlbedoSpec = vec4(albedo,1.0);

    float metallic = 0.0;
    float roughness = 0.9;
    float ao = 1.0;
    gMaterial = vec4(metallic, roughness, ao, 1.0);
}

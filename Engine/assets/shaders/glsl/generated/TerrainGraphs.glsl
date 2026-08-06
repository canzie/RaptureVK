/**
 * @brief Generated surface graph functions and their dispatcher
 * @author Rapture Material Graph Compiler
 * @version 4
 * @note DO NOT EDIT, this file is regenerated from material graphs
 */

#ifndef TERRAIN_GRAPHS_GLSL
#define TERRAIN_GRAPHS_GLSL

struct TerrainSurfaceData {
    vec3 albedo;
    vec3 normal;
    float roughness;
    float metallic;
    float ao;
    float specular;
    vec4 emission;
    float emissiveStrength;
    uint shadingModelId;
};

TerrainSurfaceData evalTerrainSurface_Terrain_1(TerrainInputs si, uint base){
TerrainSurfaceData surf;
surf.albedo=vec3(1.0);
surf.normal=normalize(si.worldNormal);
surf.roughness=0.5;
surf.metallic=0.0;
surf.ao=1.0;
surf.specular=0.04;
surf.emission=vec4(1.0);
surf.emissiveStrength=0.0;
surf.shadingModelId=SM_OPENPBR_STANDARD;
return surf;
}

TerrainSurfaceData evalTerrainSurface_TerrainLayered_3(TerrainInputs si, uint base){
TerrainSurfaceData surf;
float _n0=(1.0 - normalize(si.worldNormal).y);
float _n1=uintBitsToFloat(u_graphPool.data[base + 0]);
float _n2=uintBitsToFloat(u_graphPool.data[base + 1]);
float _n3=smoothstep(_n2, _n1, _n0);
float _n4=uintBitsToFloat(u_graphPool.data[base + 2]);
float _n5=_n4 - _n3;
float _n6=si.normalizedHeight;
float _n7=uintBitsToFloat(u_graphPool.data[base + 3]);
float _n8=uintBitsToFloat(u_graphPool.data[base + 4]);
float _n9=smoothstep(_n8, _n7, _n6);
float _n10=_n9 * _n5;
vec3 _n11=vec3(uintBitsToFloat(u_graphPool.data[base + 7]), uintBitsToFloat(u_graphPool.data[base + 8]), uintBitsToFloat(u_graphPool.data[base + 9]));
vec3 _n12=vec3(uintBitsToFloat(u_graphPool.data[base + 10]), uintBitsToFloat(u_graphPool.data[base + 11]), uintBitsToFloat(u_graphPool.data[base + 12]));
float _n13=uintBitsToFloat(u_graphPool.data[base + 13]);
float _n14=si.curvature;
float _n15=_n14 * _n13;
float _n16=clamp(_n15, float(0.0), float(1.0));
vec3 _n17=vec3(uintBitsToFloat(u_graphPool.data[base + 14]), uintBitsToFloat(u_graphPool.data[base + 15]), uintBitsToFloat(u_graphPool.data[base + 16]));
float _n18=texture(u_textures[nonuniformEXT(si.erosionTex)], si.uv).r;
vec3 _n19=vec3(uintBitsToFloat(u_graphPool.data[base + 17]), uintBitsToFloat(u_graphPool.data[base + 18]), uintBitsToFloat(u_graphPool.data[base + 19]));
vec3 _n20=vec3(uintBitsToFloat(u_graphPool.data[base + 20]), uintBitsToFloat(u_graphPool.data[base + 21]), uintBitsToFloat(u_graphPool.data[base + 22]));
vec3 _n21=mix(_n20, _n19, _n18);
vec3 _n22=mix(_n21, _n17, _n16);
vec3 _n23=mix(_n22, _n12, _n3);
vec3 _n24=mix(_n23, _n11, _n10);
float _n25=uintBitsToFloat(u_graphPool.data[base + 5]);
float _n26=uintBitsToFloat(u_graphPool.data[base + 6]);
float _n27=mix(_n26, _n25, _n10);
surf.albedo=_n24;
surf.normal=normalize(si.worldNormal);
surf.roughness=_n27;
surf.metallic=0.0;
surf.ao=1.0;
surf.specular=0.04;
surf.emission=vec4(1.0);
surf.emissiveStrength=0.0;
surf.shadingModelId=SM_OPENPBR_STANDARD;
return surf;
}

TerrainSurfaceData evalTerrainSurfaceGraph(uint graphId, TerrainInputs si, uint base) {
    switch (graphId) {
        case 1u: return evalTerrainSurface_Terrain_1(si, base);
        case 3u: return evalTerrainSurface_TerrainLayered_3(si, base);
    }

    TerrainSurfaceData surf;
    surf.albedo = vec3(1.0, 0.0, 1.0);
    surf.normal = normalize(si.worldNormal);
    surf.roughness = 0.5;
    surf.metallic = 0.0;
    surf.ao = 1.0;
    surf.specular = 0.04;
    surf.emission = vec4(0.0);
    surf.emissiveStrength = 0.0;
    surf.shadingModelId = SM_OPENPBR_STANDARD;
    return surf;
}

#endif // TERRAIN_GRAPHS_GLSL

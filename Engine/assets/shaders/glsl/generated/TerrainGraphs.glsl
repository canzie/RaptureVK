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

TerrainSurfaceData evalTerrainSurfaceGraph(uint graphId, TerrainInputs si, uint base) {
    switch (graphId) {
        case 1u: return evalTerrainSurface_Terrain_1(si, base);
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

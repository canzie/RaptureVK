/**
 * @brief Generated surface graph functions and their dispatcher
 * @author Rapture Material Graph Compiler
 * @version 4
 * @note DO NOT EDIT, this file is regenerated from material graphs
 */

#ifndef SURFACE_GRAPHS_DIFFUSE_GLSL
#define SURFACE_GRAPHS_DIFFUSE_GLSL

struct SurfaceDataDiffuse {
    vec3 albedo;
    vec3 normal;
    vec4 emission;
    float emissiveStrength;
};

SurfaceDataDiffuse evalSurfaceDiffuse_Default_0(SurfaceInputs si, uint base){
SurfaceDataDiffuse surf;
surf.albedo=vec3(1.0);
surf.normal=normalize(si.worldNormal);
surf.emission=vec4(1.0);
surf.emissiveStrength=0.0;
return surf;
}

SurfaceDataDiffuse evalSurfaceDiffuse_glTF_2(SurfaceInputs si, uint base){
SurfaceDataDiffuse surf;
vec4 _n0=vec4(uintBitsToFloat(u_graphPool.data[base + 11]), uintBitsToFloat(u_graphPool.data[base + 12]), uintBitsToFloat(u_graphPool.data[base + 13]), uintBitsToFloat(u_graphPool.data[base + 14]));
vec2 _n1=si.uv;
vec4 _n2=texture(u_textures[nonuniformEXT(u_graphPool.data[base + 15])], _n1);
vec3 _n3=(_n2).xyz * (_n0).xyz;
vec4 _n4=texture(u_textures[nonuniformEXT(u_graphPool.data[base + 10])], _n1);
vec3 _n5=normalize(mat3(normalize(si.tangent), normalize(si.bitangent), normalize(si.worldNormal)) * reconstructNormalZ(((_n4).xyz).xy));
vec3 _n6=vec3(uintBitsToFloat(u_graphPool.data[base + 1]), uintBitsToFloat(u_graphPool.data[base + 2]), uintBitsToFloat(u_graphPool.data[base + 3]));
vec4 _n7=texture(u_textures[nonuniformEXT(u_graphPool.data[base + 4])], _n1);
vec3 _n8=(_n7).xyz * _n6;
float _n9=uintBitsToFloat(u_graphPool.data[base + 0]);
surf.albedo=_n3;
surf.normal=_n5;
surf.emission=vec4(_n8, 1.0);
surf.emissiveStrength=_n9;
return surf;
}

SurfaceDataDiffuse evalSurfaceGraphDiffuse(uint graphId, SurfaceInputs si, uint base) {
    switch (graphId) {
        case 0u: return evalSurfaceDiffuse_Default_0(si, base);
        case 2u: return evalSurfaceDiffuse_glTF_2(si, base);
    }

    SurfaceDataDiffuse surf;
    surf.albedo = vec3(1.0, 0.0, 1.0);
    surf.normal = normalize(si.worldNormal);
    surf.emission = vec4(0.0);
    surf.emissiveStrength = 0.0;
    return surf;
}

#endif // SURFACE_GRAPHS_DIFFUSE_GLSL

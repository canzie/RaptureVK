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

SurfaceDataDiffuse evalSurfaceDiffuse_Grid_3(SurfaceInputs si, uint base){
SurfaceDataDiffuse surf;
vec3 _n0=vec3(uintBitsToFloat(u_graphPool.data[base + 0]), uintBitsToFloat(u_graphPool.data[base + 1]), uintBitsToFloat(u_graphPool.data[base + 2]));
vec3 _n1=vec3(uintBitsToFloat(u_graphPool.data[base + 3]), uintBitsToFloat(u_graphPool.data[base + 4]), uintBitsToFloat(u_graphPool.data[base + 5]));
vec3 _n2=vec3(uintBitsToFloat(u_graphPool.data[base + 6]), uintBitsToFloat(u_graphPool.data[base + 7]), uintBitsToFloat(u_graphPool.data[base + 8]));
vec3 _n3=_n1 / _n2;
vec3 _n4=_n3 * _n0;
vec3 _n5=si.worldPos;
vec3 _n6=_n5 + _n4;
vec3 _n7=_n6 / _n1;
vec3 _n8=fract(_n7);
vec3 _n9=_n8 - _n0;
vec3 _n10=abs(_n9);
float _n11=(_n10).x;
float _n12=(_n10).y;
float _n13=(_n10).z;
float _n14=max(_n11, _n12);
float _n15=max(_n14, _n13);
float _n16=uintBitsToFloat(u_graphPool.data[base + 9]);
float _n17=uintBitsToFloat(u_graphPool.data[base + 10]);
float _n18=_n16 - _n17;
float _n19=smoothstep(_n18, _n16, _n15);
vec3 _n20=vec3(uintBitsToFloat(u_graphPool.data[base + 11]), uintBitsToFloat(u_graphPool.data[base + 12]), uintBitsToFloat(u_graphPool.data[base + 13]));
vec3 _n21=_n6 / _n3;
vec3 _n22=fract(_n21);
vec3 _n23=_n22 - _n0;
vec3 _n24=abs(_n23);
float _n25=(_n24).x;
float _n26=(_n24).y;
float _n27=(_n24).z;
float _n28=max(_n25, _n26);
float _n29=max(_n28, _n27);
float _n30=uintBitsToFloat(u_graphPool.data[base + 14]);
float _n31=_n16 - _n30;
float _n32=smoothstep(_n31, _n16, _n29);
vec3 _n33=vec3(uintBitsToFloat(u_graphPool.data[base + 15]), uintBitsToFloat(u_graphPool.data[base + 16]), uintBitsToFloat(u_graphPool.data[base + 17]));
vec3 _n34=vec3(uintBitsToFloat(u_graphPool.data[base + 18]), uintBitsToFloat(u_graphPool.data[base + 19]), uintBitsToFloat(u_graphPool.data[base + 20]));
vec3 _n35=mix(_n34, _n33, _n32);
vec3 _n36=mix(_n35, _n20, _n19);
surf.albedo=_n36;
surf.normal=normalize(si.worldNormal);
surf.emission=vec4(1.0);
surf.emissiveStrength=0.0;
return surf;
}

SurfaceDataDiffuse evalSurfaceGraphDiffuse(uint graphId, SurfaceInputs si, uint base) {
    switch (graphId) {
        case 0u: return evalSurfaceDiffuse_Default_0(si, base);
        case 2u: return evalSurfaceDiffuse_glTF_2(si, base);
        case 3u: return evalSurfaceDiffuse_Grid_3(si, base);
    }

    SurfaceDataDiffuse surf;
    surf.albedo = vec3(1.0, 0.0, 1.0);
    surf.normal = normalize(si.worldNormal);
    surf.emission = vec4(0.0);
    surf.emissiveStrength = 0.0;
    return surf;
}

#endif // SURFACE_GRAPHS_DIFFUSE_GLSL

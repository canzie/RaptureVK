/**
 * @file SurfaceGraphs.glsl
 * @brief Generated surface graph functions and their dispatcher
 * @author Rapture Material Graph Compiler
 * @version 1
 * @note DO NOT EDIT, this file is regenerated from material graphs
 */

#ifndef SURFACE_GRAPHS_GLSL
#define SURFACE_GRAPHS_GLSL

SurfaceData evalSurface_Graph0(SurfaceInputs si, uint gii){
SurfaceData surf;
vec3 _n0=si.worldPos;
vec3 _n1=u_graphData.instances[gii].constants[0].xyz;
vec3 _n2=_n0 * _n1;
vec3 _n3=fract(_n2);
vec3 _n4=u_graphData.instances[gii].constants[1].xyz;
vec3 _n5=_n3 * _n4;
surf.albedo=_n5;
surf.normal=normalize(si.worldNormal);
surf.roughness=0.5;
surf.metallic=0.0;
surf.ao=1.0;
surf.shadingModelId=SM_OPENPBR_STANDARD;
return surf;
}

SurfaceData evalSurface_Graph1(SurfaceInputs si, uint gii){
SurfaceData surf;
vec3 _n0=u_graphData.instances[gii].constants[0].xyz;
vec3 _n1=u_graphData.instances[gii].constants[1].xyz;
vec2 _n2=si.uv;
float _n3=(_n2).x;
float _n4=(_n2).y;
float _n5=u_graphData.instances[gii].constants[2].x;
float _n6=_n3 * _n5;
float _n7=_n4 * _n5;
float _n8=_n6 + _n7;
float _n9=sin(_n8);
float _n10=u_graphData.instances[gii].constants[3].x;
float _n11=(0.0 + (1.0 - 0.0) * ((_n9 - _n10) / (1.0 - _n10)));
vec3 _n12=mix(_n0, _n1, _n11);
float _n13=dot(_n12, vec3(0.2126, 0.7152, 0.0722));
surf.albedo=_n12;
surf.normal=normalize(si.worldNormal);
surf.roughness=_n13;
surf.metallic=0.0;
surf.ao=1.0;
surf.shadingModelId=SM_OPENPBR_STANDARD;
return surf;
}

SurfaceData evalSurfaceGraph(uint graphId, SurfaceInputs si, uint gii) {
    switch (graphId) {
        case 0u: return evalSurface_Graph0(si, gii);
        case 1u: return evalSurface_Graph1(si, gii);
    }

    SurfaceData surf;
    surf.albedo = vec3(1.0, 0.0, 1.0);
    surf.normal = normalize(si.worldNormal);
    surf.roughness = 0.5;
    surf.metallic = 0.0;
    surf.ao = 1.0;
    surf.shadingModelId = SM_OPENPBR_STANDARD;
    return surf;
}

#endif // SURFACE_GRAPHS_GLSL

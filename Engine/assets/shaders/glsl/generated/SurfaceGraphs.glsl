/**
 * @file SurfaceGraphs.glsl
 * @brief Generated surface graph functions and their dispatcher
 * @author Rapture Material Graph Compiler
 * @version 2
 * @note DO NOT EDIT, this file is regenerated from material graphs
 */

#ifndef SURFACE_GRAPHS_GLSL
#define SURFACE_GRAPHS_GLSL

SurfaceData evalSurface_Graph0_0(SurfaceInputs si, uint base){
SurfaceData surf;
vec3 _n0=vec3(uintBitsToFloat(u_graphPool.data[base + 0]), uintBitsToFloat(u_graphPool.data[base + 1]), uintBitsToFloat(u_graphPool.data[base + 2]));
vec3 _n1=vec3(uintBitsToFloat(u_graphPool.data[base + 3]), uintBitsToFloat(u_graphPool.data[base + 4]), uintBitsToFloat(u_graphPool.data[base + 5]));
vec3 _n2=si.worldPos;
vec3 _n3=_n2 * _n1;
vec3 _n4=fract(_n3);
vec3 _n5=_n4 * _n0;
surf.albedo=_n5;
surf.normal=normalize(si.worldNormal);
surf.roughness=0.5;
surf.metallic=0.0;
surf.ao=1.0;
surf.shadingModelId=SM_OPENPBR_STANDARD;
return surf;
}

SurfaceData evalSurface_Graph1_1(SurfaceInputs si, uint base){
SurfaceData surf;
float _n0=uintBitsToFloat(u_graphPool.data[base + 0]);
float _n1=uintBitsToFloat(u_graphPool.data[base + 1]);
vec2 _n2=si.uv;
float _n3=(_n2).x;
float _n4=(_n2).y;
float _n5=_n4 * _n1;
float _n6=_n3 * _n1;
float _n7=_n6 + _n5;
float _n8=sin(_n7);
float _n9=(0.0 + (1.0 - 0.0) * ((_n8 - _n0) / (1.0 - _n0)));
vec3 _n10=vec3(uintBitsToFloat(u_graphPool.data[base + 2]), uintBitsToFloat(u_graphPool.data[base + 3]), uintBitsToFloat(u_graphPool.data[base + 4]));
vec3 _n11=vec3(uintBitsToFloat(u_graphPool.data[base + 5]), uintBitsToFloat(u_graphPool.data[base + 6]), uintBitsToFloat(u_graphPool.data[base + 7]));
vec3 _n12=mix(_n11, _n10, _n9);
float _n13=dot(_n12, vec3(0.2126, 0.7152, 0.0722));
surf.albedo=_n12;
surf.normal=normalize(si.worldNormal);
surf.roughness=_n13;
surf.metallic=0.0;
surf.ao=1.0;
surf.shadingModelId=SM_OPENPBR_STANDARD;
return surf;
}

SurfaceData evalSurfaceGraph(uint graphId, SurfaceInputs si, uint base) {
    switch (graphId) {
        case 0u: return evalSurface_Graph0_0(si, base);
        case 1u: return evalSurface_Graph1_1(si, base);
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

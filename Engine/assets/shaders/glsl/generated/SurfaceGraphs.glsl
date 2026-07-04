// SurfaceGraphs.glsl - GENERATED
// Placeholder: hand-written until the graph compiler (Phase 2b) emits this file.
// Depends on MaterialCommon.glsl (SurfaceInputs/SurfaceData, u_graphData, SM_*).

#ifndef SURFACE_GRAPHS_GLSL
#define SURFACE_GRAPHS_GLSL

// Graph 0: test - fract(worldPos) tinted by constants[0]
SurfaceData evalSurface_Test(SurfaceInputs si, uint gii) {
    SurfaceData surf;
    vec3 tint = u_graphData.instances[gii].constants[0].rgb;
    surf.albedo = fract(si.worldPos * 0.5) * tint;
    surf.normal = normalize(si.worldNormal);
    surf.roughness = 0.6;
    surf.metallic = 0.0;
    surf.ao = 1.0;
    surf.shadingModelId = SM_OPENPBR_STANDARD;
    return surf;
}

SurfaceData evalSurfaceGraph(uint graphId, SurfaceInputs si, uint gii) {
    switch (graphId) {
        case 0u: return evalSurface_Test(si, gii);
    }

    // Unknown graph -> magenta error surface
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

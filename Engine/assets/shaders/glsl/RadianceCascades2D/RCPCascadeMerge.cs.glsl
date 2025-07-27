#version 460 core

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Merge cascadeLevel + 1 into cascadeLevel
// we start at n-1, because cascade n has no previous cascade
// we do this until we reach cascade 0
// cascade 0 will then contain the final result

layout(set = 4, binding = 0, rgba32f) uniform restrict image2D cascadeN;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

// Push constants (updated structure)
layout(push_constant) uniform PushConstants {
    uint u_prevCascadeIndex; // index of cascade n+1, index into gTextureArrays
    uint u_currentCascadeIndex; // index of current cascade (n)
};

#include "RCCommon.glsl"
#include "TraceRay.glsl"

layout(std140, set = 0, binding = 7) uniform CascadeLevelInfos {
    CascadeLevelInfo cascadeLevelInfo;
} cs[];

rayQueryEXT query;


vec4 getClosestHit(vec2 origin, vec2 direction, vec2 interval) {

    rayQueryInitializeEXT(
        query,
        topLevelAS[0], // TODO: make this the correct index
        gl_RayFlagsOpaqueEXT,
        0xFF,
        vec3(origin.x, 0.0, origin.y),
        interval.x,
        vec3(direction.x, 0.0, direction.y),
        interval.y
    );

    while (rayQueryProceedEXT(query)) {}

    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        float hitT = rayQueryGetIntersectionTEXT(query, true);
        uint primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
        vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(query, true);
        uint instanceCustomIndex = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);

        MeshInfo meshInfo = u_sceneInfo.MeshInfos[instanceCustomIndex];
        
        // Get surface data for the hit point
        SurfaceData surface = getSurfaceDataForHit(primitiveID, barycentrics, meshInfo);
        
        vec3 emissiveColor = sampleEmissiveColor(meshInfo, surface.texCoord);
        vec3 albedo = sampleAlbedo(meshInfo, surface.texCoord);
        float dstSquared = hitT * hitT;
        float intensity = 1.0 / (hitT + 0.01);

        emissiveColor = emissiveColor * intensity;


        vec3 radiance = emissiveColor;

        return vec4(radiance, 0.0); // hit
    }

    return vec4(0.0, 0.0, 0.0, 1.0); // miss

}

vec2 getUniformDirection(uint sampleIndex, uint numSamples) {
    float angle = (2.0 * 3.141592653 * (sampleIndex + 0.5)) / numSamples;
    return normalize(vec2(cos(angle), sin(angle)));
}

vec4 merge_intervals(vec4 near, vec4 far) {
    /* Far radiance can get occluded by near visibility term */
    const vec3 radiance = near.rgb + (far.rgb * near.a);

    return vec4(radiance, near.a * far.a);
}

void main() {
    CascadeLevelInfo currentCascadeInfo = cs[u_currentCascadeIndex].cascadeLevelInfo;
    CascadeLevelInfo prevCascadeInfo = cs[u_prevCascadeIndex].cascadeLevelInfo;
    
    uvec2 outputCoords = uvec2(gl_GlobalInvocationID.xy);

    uvec2 imageSize = uvec2(imageSize(cascadeN));
    if (outputCoords.x >= imageSize.x || 
        outputCoords.y >= imageSize.y) { 
        return;
    }


    ivec2 ray_dir_coord = ivec2(outputCoords) % ivec2(currentCascadeInfo.angularResolution);
    int ray_dir_index = ray_dir_coord.x + ray_dir_coord.y * int(currentCascadeInfo.angularResolution);
    ivec2 ray_dir_coord_prev = ray_dir_coord * int(prevCascadeInfo.angularResolution / currentCascadeInfo.angularResolution);
    int ray_dir_index_prev = ray_dir_coord_prev.x + ray_dir_coord_prev.y * int(prevCascadeInfo.angularResolution);

    // probe coordinate in the current cascade
    ivec2 probeCoord = ivec2(outputCoords) / ivec2(currentCascadeInfo.angularResolution);

    // this makes sure that when we floor, we end up at the correct topleft.
    vec2 probeCoordF = vec2(probeCoord) - 0.5;
    

    vec2 normProbeCoords = probeCoordF / currentCascadeInfo.probeGridDimensions;
    vec2 prevProbeCoordsF = normProbeCoords * vec2(prevCascadeInfo.probeGridDimensions);


    // this is the top left probe coordinate of the closest 2x2 grid around the current probe(cascade n), in the previous cascade(n+1)
    ivec2 prevProbeBaseCoords = ivec2(floor(prevProbeCoordsF));

    vec2 w = fract(prevProbeCoordsF); // 0.25 or 0.75, both x or y -> 4 posible combos
    //vec2 smooth_weights = smoothstep(vec2(0.0), vec2(1.0), w);
    //w = smooth_weights;


    // need to add 0.5, since when going to a higher grid we could be off by one
    // e.g. (2, 3) in a 8x8 grid -> (4, 6) != (5, 7) in a 16x16 grid

    // usually x2 angular res in the (prev) cascade (c_n+1)
    vec2 prevRayCoordsF = (vec2(ray_dir_coord) + 0.5) * (vec2(prevCascadeInfo.angularResolution) / vec2(currentCascadeInfo.angularResolution));
    prevRayCoordsF -= 0.5; // prevent off by one error at the end (positive) of the grid
    ivec2 prevRayBaseCoords = ivec2(floor(prevRayCoordsF));

    ivec2 offsets[4];
    offsets[0] = ivec2(0, 0); 
    offsets[1] = ivec2(1, 0);
    offsets[2] = ivec2(0, 1);
    offsets[3] = ivec2(1, 1);
    
    vec4 merged = vec4(0.0);

    vec2 currentProbeWorldPosition = vec2(probeCoord) * currentCascadeInfo.probeSpacing;

    for (int r = 0; r < 4; ++r) {
        ivec2 rayOffset = offsets[r];
        ivec2 sampleRayCoord = prevRayBaseCoords + rayOffset;
        sampleRayCoord = clamp(sampleRayCoord, ivec2(0), ivec2(prevCascadeInfo.angularResolution - 1));
        int sampleRayIndex1D = sampleRayCoord.x + sampleRayCoord.y * int(prevCascadeInfo.angularResolution);
        // cast ray from current probe, with the interval of the previous cascade
        // in the direction of sampleRayCoord
        // then use the result from this hit to merge_intervals, instead of currentData
        vec2 direction = getUniformDirection(sampleRayIndex1D, prevCascadeInfo.angularResolution*prevCascadeInfo.angularResolution);
        vec4 hit = getClosestHit(currentProbeWorldPosition, direction, vec2(prevCascadeInfo.probeSpacing.x, currentCascadeInfo.maxProbeDistance));

        if (hit.a == 0.0) {
            merged += hit;
            continue;
        }


        vec4 si[4];

        for (int probeIdx = 0; probeIdx < 4; ++probeIdx) {
            ivec2 prevProbeOffset = ivec2(offsets[probeIdx]);
            ivec2 prevProbeCoords = prevProbeBaseCoords + prevProbeOffset;
            prevProbeCoords = clamp(prevProbeCoords, ivec2(0), ivec2(prevCascadeInfo.probeGridDimensions - 1));

            ivec2 prevTextCoordsBase = prevProbeCoords * ivec2(prevCascadeInfo.angularResolution);
            ivec2 prevTextCoords = prevTextCoordsBase + sampleRayCoord;

            vec4 data = texelFetch(gTextures[prevCascadeInfo.cascadeTextureIndex], prevTextCoords, 0);
            data.a = 1.0 - data.a;
            si[probeIdx] = data;
        }

        // Bilinear interpolate across the 4 probe samples for this ray
        vec4 interpolated = mix(mix(si[0], si[1], w.x), mix(si[2], si[3], w.x), w.y);

        // Merge with current data
        merged += merge_intervals(hit, interpolated);
    }

    // Final average over the 4 rays
    vec4 final_radiance = merged / 4.0;

    // Store final merged result. Convert final visibility back to opacity.
    imageStore(cascadeN, ivec2(outputCoords), vec4(final_radiance.rgb, 1.0 - final_radiance.a));
}
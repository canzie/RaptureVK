/*
    Most of the logic is yoinked from the RTXGI repository, then adapted to glsl for use in my engine.
*/

#ifdef PI
#else
    #define PI 3.14159265359
#endif

struct SunProperties {

    vec4 position;      // w = light type (0 = point, 1 = directional, 2 = spot)
    vec4 direction;     // w = range
    vec4 color;         // w = intensity
    vec4 spotAngles;    // x = inner cone cos, y = outer cone cos, z = entity id, w = unused

};

#ifdef DDGI_ENABLE_DIFFUSE_LIGHTING

/**
 * Computes the visibility factor for a given vector to a light using ray tracing.
 */
float LightVisibility(
    vec3 worldPosition,
    vec3 normal,
    vec3 lightVector,
    float tmax,
    float normalBias,
    float viewBias)
{
    // Create a visibility ray query
    rayQueryEXT visibilityQuery;
    
    vec3 rayOrigin = worldPosition + (normal * normalBias); // TODO: not using viewBias yet
    vec3 rayDirection = normalize(lightVector);
    
    // Initialize ray query for visibility test
    // Use ACCEPT_FIRST_HIT and SKIP_CLOSEST_HIT equivalent flags
    rayQueryInitializeEXT(
        visibilityQuery, 
        topLevelAS[0], 
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 
        0xFF,
        rayOrigin, 
        0.0, 
        rayDirection, 
        tmax
    );
    
    // Process the ray query
    rayQueryProceedEXT(visibilityQuery);
    
    // Check if we hit anything (if we did, the light is occluded)
    if (rayQueryGetIntersectionTypeEXT(visibilityQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        return 0.0; // Occluded
    }
    
    return 1.0; // Visible
}

/**
 * Evaluate direct lighting for the current surface and the directional light using ray-traced visibility.
 */
vec3 EvaluateDirectionalLight(vec3 shadingNormal, vec3 hitPositionWorld, SunProperties sunProperties) {   
    
    // Use ray-traced visibility instead of shadow maps
    float normalBias = 0.01; // Small bias to avoid self-intersection
    float viewBias = 0.0;    // Not used yet, but kept for future
    
    float visibility = LightVisibility(
        hitPositionWorld,
        shadingNormal,
        -sunProperties.direction.xyz,  // Light vector (towards sun)
        1e27,                              // Very large tmax for directional light
        normalBias,
        viewBias
    );
    
    // Early out if the light isn't visible from the surface
    if (visibility <= 0.0) {
        return vec3(0.0);
    }
    
    // Compute lighting
    vec3 lightDirection = normalize(-sunProperties.direction.xyz);
    float NdotL = max(dot(shadingNormal, lightDirection), 0.0);
    
    return sunProperties.color.xyz * sunProperties.color.w * NdotL * visibility;
}

vec3 EvaluateSpotLight(vec3 shadingNormal, vec3 hitPositionWorld, SunProperties sunProperties)
{   

    return vec3(0.0);
}


/**
 * Computes the diffuse reflection of light off the given surface (direct lighting).
 */
vec3 DirectDiffuseLighting(vec3 albedo, vec3 shadingNormal, vec3 hitPositionWorld, SunProperties sunProperties) {

    vec3 brdf = (albedo / PI);

    vec3 lighting = EvaluateDirectionalLight(shadingNormal, hitPositionWorld, sunProperties);


    return (brdf * lighting);
}

#endif



// New function to calculate indirect diffuse from the single nearest probe
vec3 DDGIGetVolumeIrradiance(
    vec3 worldPosition, 
    vec3 direction, 
    vec3 surfaceBias, 
    sampler2DArray probeIrradianceAtlas,
    sampler2DArray probeDistanceAtlas,
    ProbeVolume volume) {

    vec3 irradiance = vec3(0.0);
    float accumulatedWeights = 0.0;
    
    vec3 biasedWorldPosition = (worldPosition + surfaceBias);

    // Get the 3D grid coordinates of the probe nearest the biased world position (i.e. the "base" probe)
    ivec3 baseProbeCoords = DDGIGetBaseProbeGridCoords(biasedWorldPosition, volume);

    // Get the world-space position of the base probe (ignore relocation)
    vec3 baseProbeWorldPosition = DDGIGetProbeWorldPosition(baseProbeCoords, volume);

    // Clamp the distance (in grid space) between the given point and the base probe's world position (on each axis) to [0, 1]
    vec3 gridSpaceDistance = (biasedWorldPosition - baseProbeWorldPosition);
    gridSpaceDistance = RTXGIQuaternionRotate(gridSpaceDistance, RTXGIQuaternionConjugate(volume.rotation));
    vec3 alpha = clamp((gridSpaceDistance / volume.spacing), vec3(0.0), vec3(1.0));


    for (int probeIndex = 0; probeIndex < 8; probeIndex++) {

        // Compute the offset to the adjacent probe in grid coordinates by
        // sourcing the offsets from the bits of the loop index: x = bit 0, y = bit 1, z = bit 2
        ivec3 adjacentProbeOffset = ivec3(probeIndex, probeIndex >> 1, probeIndex >> 2) & ivec3(1, 1, 1);

        // Get the 3D grid coordinates of the adjacent probe by adding the offset to 
        // the base probe and clamping to the grid boundaries
        ivec3 adjacentProbeCoords = clamp(baseProbeCoords + adjacentProbeOffset, ivec3(0, 0, 0), ivec3(volume.gridDimensions - ivec3(1, 1, 1)));

        // Get the adjacent probe's index, adjusting the adjacent probe index for scrolling offsets (if present)
        int adjacentProbeIndex = DDGIGetProbeIndex(adjacentProbeCoords, volume);

        vec3 adjacentProbeWorldPosition = DDGIGetProbeWorldPosition(adjacentProbeCoords, volume);

        // Compute the distance and direction from the (biased and non-biased) shading point and the adjacent probe
        vec3  worldPosToAdjProbe = normalize(adjacentProbeWorldPosition - worldPosition);
        vec3  biasedPosToAdjProbe = normalize(adjacentProbeWorldPosition - biasedWorldPosition);
        float biasedPosToAdjProbeDist = length(adjacentProbeWorldPosition - biasedWorldPosition);
        
        // Compute trilinear weights based on the distance to each adjacent probe
        // to smoothly transition between probes. adjacentProbeOffset is binary, so we're
        // using a 1-alpha when adjacentProbeOffset = 0 and alpha when adjacentProbeOffset = 1.
        vec3 trilinear = max(vec3(0.001), mix(vec3(1.0) - alpha, alpha, vec3(adjacentProbeOffset)));  
        float trilinearWeight = (trilinear.x * trilinear.y * trilinear.z);

        float weight = 1.0;

        // A naive soft backface weight would ignore a probe when
        // it is behind the surface. That's good for walls, but for
        // small details inside of a room, the normals on the details
        // might rule out all of the probes that have mutual visibility 
        // to the point. We instead use a "wrap shading" test. The small
        // offset at the end reduces the "going to zero" impact.
        float wrapShading = (dot(worldPosToAdjProbe, direction) + 1.0) * 0.5;
        weight *= (wrapShading * wrapShading) + 0.2;

        // Compute the octahedral coordinates of the adjacent probe
        vec2 octantCoords = DDGIGetOctahedralCoordinates(-biasedPosToAdjProbe);

        // Get the texture array coordinates for the octant of the probe
        vec3 probeTextureUV = DDGIGetProbeUV(adjacentProbeIndex, octantCoords, volume.probeNumDistanceInteriorTexels, volume);

        // Sample the probe's distance texture to get the mean distance to nearby surfaces
        // Note: Multiplied by 2.0 to compensate for the division by 2 in the blending shader
        vec2 filteredDistance = 2.0 * texture(probeDistanceAtlas, probeTextureUV).rg;



        // Find the variance of the mean distance
        float variance = abs((filteredDistance.x * filteredDistance.x) - filteredDistance.y);

        // Occlusion test
        float chebyshevWeight = 1.0;
        if(biasedPosToAdjProbeDist > filteredDistance.x) // occluded
        {
            // v must be greater than 0, which is guaranteed by the if condition above.
            float v = biasedPosToAdjProbeDist - filteredDistance.x;
            chebyshevWeight = variance / (variance + (v * v));

            // Increase the contrast in the weight
            chebyshevWeight = max((chebyshevWeight * chebyshevWeight * chebyshevWeight), 0.0);
        }
        
        // Avoid visibility weights ever going all the way to zero because
        // when *no* probe has visibility we need a fallback value
        weight *= max(0.01, chebyshevWeight);

        // Avoid a weight of zero
        weight = max(0.000001, weight);

        // A small amount of light is visible due to logarithmic perception, so
        // crush tiny weights but keep the curve continuous
        const float crushThreshold = 0.2;
        if (weight < crushThreshold)
        {
            weight *= (weight * weight) * (1.0 / (crushThreshold * crushThreshold));
        }

        // Apply the trilinear weights
        weight *= trilinearWeight;

        // Get the octahedral coordinates for the sample direction
        octantCoords = DDGIGetOctahedralCoordinates(direction);

        // Get the probe's texture coordinates
        probeTextureUV = DDGIGetProbeUV(adjacentProbeIndex, octantCoords, volume.probeNumIrradianceInteriorTexels, volume);

        // Sample the probe's irradiance
        vec3 probeIrradiance = texture(probeIrradianceAtlas, probeTextureUV).rgb;



        // Check for invalid probe data (black probes indicate probes inside geometry)
        // Use length check instead of individual component checks to handle small but valid values
        float probeLength = length(probeIrradiance);
        if (probeLength < 0.001) {
            // Reduce weight but don't completely skip to maintain smooth transitions
            weight *= 0.1;
        }

        // Decode the tone curve, but leave a gamma = 2 curve to approximate sRGB blending
        vec3 exponent = vec3(volume.probeIrradianceEncodingGamma * 0.5);
        probeIrradiance = pow(probeIrradiance, exponent);

        // Accumulate the weighted irradiance
        irradiance += (weight * probeIrradiance);
        accumulatedWeights += weight;
            
    }

    if(accumulatedWeights == 0.0) return vec3(0.0);

    irradiance *= (1.0 / accumulatedWeights);   // Normalize by the accumulated weights
    irradiance *= irradiance;                   // Go back to linear irradiance
    irradiance *= 6.28318530718;                    // Multiply by the area of the integration domain (hemisphere) to complete the Monte Carlo Estimator equation
    //irradiance *= 1.0989;
    return irradiance;
}

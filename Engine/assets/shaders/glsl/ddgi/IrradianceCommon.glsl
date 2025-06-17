/*
    Most of the logic is yoinked from the RTXGI repository, then adapted to glsl for use in my engine.
*/

#ifdef PI
#else
    #define PI 3.14159265359
#endif

struct SunProperties {
    mat4 sunLightSpaceMatrix;
    vec3 sunDirectionWorld;
    vec3 sunColor;

    float sunIntensity;
    uint sunShadowTextureArrayIndex;
};

#ifdef DDGI_ENABLE_DIFFUSE_LIGHTING

float calculateSunShadowFactor(vec3 hitPositionWorld, vec3 hitNormalWorld, SunProperties sunProperties) {
    if (sunProperties.sunShadowTextureArrayIndex == 0) return 1.0; // No shadow map or index is zero

    // Transform hit position to light clip space for the largest cascade
    vec4 hitPosLightSpace = sunProperties.sunLightSpaceMatrix * vec4(hitPositionWorld, 1.0);

    // Perform perspective divide
    vec3 projCoords = hitPosLightSpace.xyz / hitPosLightSpace.w;

    // Transform to [0,1] range for texture lookup
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Check if fragment is outside the light's view frustum [0, 1] range
    if(projCoords.x < 0.0 || projCoords.x > 1.0 ||
       projCoords.y < 0.0 || projCoords.y > 1.0 ||
       projCoords.z < 0.0 || projCoords.z > 1.0) { // Check Z too
        return 1.0; // Outside frustum = Not shadowed
    }

    vec3 lightWorldDir = normalize(-sunProperties.sunDirectionWorld);
    float cosTheta = clamp(dot(hitNormalWorld, lightWorldDir), 0.0, 1.0);
    float bias = max(0.005 * (1.0 - cosTheta), 0.001);

    float comparisonDepth = projCoords.z - bias;
    
    vec2 texelSize = 1.0 / textureSize(gShadowMaps[sunProperties.sunShadowTextureArrayIndex], 0);

    // Use descriptor array to access shadow map
    float shadowFactor = 0.0;

    // Use a 3x3 kernel for PCF
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            shadowFactor += texture(gShadowMaps[sunProperties.sunShadowTextureArrayIndex], vec3(
                projCoords.xy + vec2(x, y) * texelSize,
                comparisonDepth
            ));
        }
    }
    shadowFactor /= 9.0; // Average the results
    
    return clamp(shadowFactor, 0.0, 1.0);
}

/**
 * Evaluate direct lighting for the current surface and the directional light.
 * should sample the shadow map for light visability
 */
vec3 EvaluateDirectionalLight(vec3 shadingNormal, vec3 hitPositionWorld, SunProperties sunProperties) {   

    // sample shadow map here
    float shadowFactor = calculateSunShadowFactor(hitPositionWorld, shadingNormal, sunProperties);
    //shadowFactor = 0.0;
    // Early out, the light isn't visible from the surface
    if (shadowFactor == 0.0){
        return vec3(0.0);
    }

    // Compute lighting
    vec3 lightDirection = normalize(-sunProperties.sunDirectionWorld);
    float NdotL = max(dot(shadingNormal, lightDirection), 0.0);

    // could maybe sample skubox here, doesnt matter much right now
    return sunProperties.sunIntensity * sunProperties.sunColor * NdotL * shadowFactor;
}

vec3 EvaluateSpotLight(vec3 shadingNormal, vec3 hitPositionWorld, SunProperties sunProperties)
{   

    return vec3(0.0);
}


/**
 * Computes the diffuse reflection of light off the given surface (direct lighting).
 */
vec3 DirectDiffuseLighting(vec3 albedo, vec3 shadingNormal, vec3 hitPositionWorld, SunProperties sunProperties)
{
    

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
    //if(!IsVolumeMovementScrolling(volume)) gridSpaceDistance = RTXGIQuaternionRotate(gridSpaceDistance, RTXGIQuaternionConjugate(volume.rotation));
    vec3 alpha = clamp((gridSpaceDistance / volume.spacing), vec3(0.0), vec3(1.0));


    for (int probeIndex = 0; probeIndex < 8; probeIndex++) {

        // Compute the offset to the adjacent probe in grid coordinates by
        // sourcing the offsets from the bits of the loop index: x = bit 0, y = bit 1, z = bit 2
        ivec3 adjacentProbeOffset = ivec3(probeIndex, probeIndex >> 1, probeIndex >> 2) & ivec3(1, 1, 1);

        // Get the 3D grid coordinates of the adjacent probe by adding the offset to 
        // the base probe and clamping to the grid boundaries
        ivec3 adjacentProbeCoords = clamp(baseProbeCoords + adjacentProbeOffset, ivec3(0, 0, 0), ivec3(volume.gridDimensions - uvec3(1, 1, 1)));

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

        // scuffed way to recognize probes inside of geometry, a better implementation would move/relocate these probes
        
        //if (probeIrradiance.x <= 0.0 || probeIrradiance.y <= 0.0 || probeIrradiance.z <= 0.0) {
        //    continue;
        //}

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

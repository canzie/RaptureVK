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

// Sun shadow uniforms
layout(std140, set=0, binding = 1) uniform SunPropertiesUBO { // use the light thing here instead
    SunProperties sunProperties;
}u_sunProperties[];

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
    return float(rayQueryGetIntersectionTypeEXT(visibilityQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT);
}

float LightFalloff(float distanceToLight) {
    return 1.0 / pow(max(distanceToLight, 1.0), 2);
}

float LightWindowing(float distanceToLight, float maxDistance) {
    return pow(clamp(1.0 - pow((distanceToLight / maxDistance), 4), 0.0, 1.0), 2);
}

float SpotAttenuation(vec3 spotDirection, vec3 lightDirection, float umbra, float penumbra)
{
    // Spot attenuation function from Frostbite, pg 115 in RTR4
    float cosTheta = clamp(dot(spotDirection, lightDirection), 0.0, 1.0);
    float t = clamp((cosTheta - cos(umbra)) / (cos(penumbra) - cos(umbra)), 0.0, 1.0);
    return t * t;
}

/**
 * Evaluate direct lighting for the current surface and the directional light using ray-traced visibility.
 */
vec3 EvaluateDirectionalLight(vec3 surfaceNormal, vec3 hitPositionWorld, SunProperties sunProperties, vec3 shadingNormal) {   
    
    // Use ray-traced visibility instead of shadow maps
    float normalBias = 0.008; // Small bias to avoid self-intersection
    float viewBias = 0.0001;    // Not used yet, but kept for future
    
    float visibility = LightVisibility(
        hitPositionWorld,
        surfaceNormal,
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
    vec3 lightDirection = -normalize(sunProperties.direction.xyz);
    float NdotL = max(dot(shadingNormal, lightDirection), 0.0);
    
    return sunProperties.color.xyz * sunProperties.color.w * NdotL * visibility;
}

vec3 EvaluateSpotLight(vec3 surfaceNormal, vec3 hitPositionWorld, SunProperties sunProperties)
{   

    float normalBias = 0.008; // Small bias to avoid self-intersection
    float viewBias = 0.0001; 

    vec3 lightVector = (sunProperties.position.xyz - hitPositionWorld);
    float lightDistance = length(lightVector);
    float lightRange = sunProperties.direction.w;

    if (lightDistance > lightRange) return vec3(0.0);

    float tmax = (lightDistance - viewBias);
    float visibility = LightVisibility(hitPositionWorld, surfaceNormal, lightVector, tmax, normalBias, viewBias);

    if (visibility <= 0.f) return vec3(0.0);

    // Compute lighting
    vec3 lightDirection = normalize(lightVector);
    float NdotL = max(dot(surfaceNormal, lightDirection), 0.0);
    vec3 spotDirection = normalize(sunProperties.direction.xyz);
    float attenuation = SpotAttenuation(spotDirection, -lightDirection, sunProperties.spotAngles.x, sunProperties.spotAngles.y);
    float falloff = LightFalloff(lightDistance);
    float window = LightWindowing(lightDistance, lightRange);

    return sunProperties.color.xyz * sunProperties.color.w * NdotL * attenuation * falloff * window * visibility;
}

vec3 EvaluatePointLight(vec3 shadingNormal, vec3 hitPositionWorld, SunProperties PointLight)
{   
    float normalBias = 0.01; // Small bias to avoid self-intersection
    float viewBias = 0.0;    // Not used yet, but kept for future

    vec3 lightVector = (PointLight.position.xyz - hitPositionWorld);
    float  lightDistance = length(lightVector);

    // Early out, light energy doesn't reach the surface
    if (lightDistance > PointLight.position.w) return vec3(0.0);

    float tmax = (lightDistance - viewBias);
    float visibility = LightVisibility(hitPositionWorld, shadingNormal, lightVector, tmax, normalBias, viewBias);

    // Early out, this light isn't visible from the surface
    if (visibility <= 0.0) return vec3(0.0);

    // Compute lighting
    vec3 lightDirection = normalize(lightVector);
    float  nol = max(dot(shadingNormal, lightDirection), 0.0);
    float  falloff = LightFalloff(lightDistance);
    float  window = LightWindowing(lightDistance, PointLight.position.w);

    vec3 color = PointLight.color.xyz * PointLight.color.w * nol * falloff * window * visibility;

    return color;
}

/**
 * Computes the diffuse reflection of light off the given surface (direct lighting).
 */
vec3 DirectDiffuseLighting(vec3 albedo, vec3 surfaceNormal, vec3 hitPositionWorld, uint lightCount, vec3 shadingNormal) {

    vec3 brdf = (albedo / PI);

    vec3 totalLighting = vec3(0.0);

    for (uint i = 0; i < lightCount; ++i) {
        SunProperties light = u_sunProperties[i].sunProperties;
        
        // light type is in light.position.w
        if (light.position.w == 1) { // Directional
             totalLighting += EvaluateDirectionalLight(surfaceNormal, hitPositionWorld, light, shadingNormal);
        } else if (light.position.w == 0) { // Point
             totalLighting += EvaluatePointLight(surfaceNormal, hitPositionWorld, light);
        } else if (light.position.w == 2) { // Spot
             totalLighting += EvaluateSpotLight(surfaceNormal, hitPositionWorld, light);
        }
    }

    return (brdf * totalLighting);
}

#endif



// New function to calculate indirect diffuse from the single nearest probe
vec3 DDGIGetVolumeIrradiance(
    vec3 worldPosition,
    vec3 direction,
    vec3 surfaceBias,
    sampler2DArray probeIrradianceAtlas,
    sampler2DArray probeDistanceAtlas,
    sampler2DArray probeOffsetAtlas,
    usampler2DArray probeClassificationAtlas,
    ProbeVolume volume) {

    vec3 irradiance = vec3(0.0);
    float accumulatedWeights = 0.0;
    
    vec3 biasedWorldPosition = (worldPosition + surfaceBias);

    // Get the 3D grid coordinates of the probe nearest the biased world position (i.e. the "base" probe)
    ivec3 baseProbeCoords = DDGIGetBaseProbeGridCoords(biasedWorldPosition, volume);

    // Get the world-space position of the base probe (ignore relocation)
    vec3 baseProbeWorldPosition = DDGIGetProbeWorldPosition(baseProbeCoords, volume, probeOffsetAtlas);

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

        uvec3 adjacentProbeTexelCoords = DDGIGetProbeTexelCoords(adjacentProbeIndex, volume);
        uint adjacentProbeState = texelFetch(probeClassificationAtlas, ivec3(adjacentProbeTexelCoords), 0).r;
        const uint PROBE_STATE_ACTIVE = 0u;
        if (adjacentProbeState != PROBE_STATE_ACTIVE) continue;

        vec3 adjacentProbeWorldPosition = DDGIGetProbeWorldPosition(adjacentProbeCoords, volume, probeOffsetAtlas);

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

        // Clamp octahedral coordinates inward by half a texel to prevent bilinear filtering from sampling border texels
        float distanceOctInset = 1.0 / float(volume.probeNumDistanceInteriorTexels);
        octantCoords = clamp(octantCoords, vec2(-1.0 + distanceOctInset), vec2(1.0 - distanceOctInset));

        // Get the texture array coordinates for the octant of the probe
        vec3 probeTextureUV = DDGIGetProbeUV(adjacentProbeIndex, octantCoords, volume.probeNumDistanceInteriorTexels, volume);

        // Sample the probe's distance texture to get the mean distance to nearby surfaces
        // Note: Multiplied by 2.0 to compensate for the division by 2 in the blending shader
        vec2 filteredDistance = 2.0 * textureLod(probeDistanceAtlas, probeTextureUV, 0.0).rg;

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

        // Clamp octahedral coordinates inward by half a texel to prevent bilinear filtering from sampling border texels
        float irradianceOctInset = 1.0 / float(volume.probeNumIrradianceInteriorTexels);
        octantCoords = clamp(octantCoords, vec2(-1.0 + irradianceOctInset), vec2(1.0 - irradianceOctInset));

        // Get the probe's texture coordinates
        probeTextureUV = DDGIGetProbeUV(adjacentProbeIndex, octantCoords, volume.probeNumIrradianceInteriorTexels, volume);

        // Sample the probe's irradiance
        vec3 probeIrradiance = textureLod(probeIrradianceAtlas, probeTextureUV, 0.0).rgb;



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
    irradiance *= 6.28318530718;                // Multiply by the area of the integration domain (hemisphere) to complete the Monte Carlo Estimator equation

    // Adjust for energy loss due to reduced precision in the R11G11B10F irradiance texture format
    // RTXGI uses 1.0989 for R10G10B10A2. R11G11B10F has similar precision issues (especially blue channel with 10 bits)
    // irradiance *= 1.0989;

    return irradiance;
}

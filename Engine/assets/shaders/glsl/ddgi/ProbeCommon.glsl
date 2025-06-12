/*
    Most of the logic is yoinked from the RTXGI repository, then adapted to glsl for use in my engine.
*/

// Global descriptor arrays for bindless textures (set 3)
#ifndef DESCRIPTOR_ARRAYS_DEFINED
#define DESCRIPTOR_ARRAYS_DEFINED
layout(set = 3, binding = 0) uniform sampler2D gTextures[];
layout(set = 3, binding = 1) uniform sampler2DShadow gShadowMaps[];
#endif

struct ProbeVolume {
    vec3 origin;

    vec4 rotation;                           // rotation quaternion for the volume
    vec4 probeRayRotation;                   // rotation quaternion for probe rays


    vec3 spacing;
    uvec3 gridDimensions;

    int      probeNumRays;                       // number of rays traced per probe
    int      probeNumIrradianceInteriorTexels;   // number of texels in one dimension of a probe's irradiance texture (does not include 1-texel border)
    int      probeNumDistanceInteriorTexels;     // number of texels in one dimension of a probe's distance texture (does not include 1-texel border)

    float    probeHysteresis;                    // weight of the previous irradiance and distance data store in probes
    float    probeMaxRayDistance;                // maximum world-space distance a probe ray can travel
    float    probeNormalBias;                    // offset along the surface normal, applied during lighting to avoid numerical instabilities when determining visibility
    float    probeViewBias;                      // offset along the camera view ray, applied during lighting to avoid numerical instabilities when determining visibility
    float    probeDistanceExponent;              // exponent used during visibility testing. High values react rapidly to depth discontinuities, but may cause banding
    float    probeIrradianceEncodingGamma;       // exponent that perceptually encodes irradiance for faster light-to-dark convergence

    float    probeBrightnessThreshold;

    // Probe Relocation, Probe Classification
    float    probeMinFrontfaceDistance;          // minimum world-space distance to a front facing triangle allowed before a probe is relocated

    float    probeRandomRayBackfaceThreshold;
    float    probeFixedRayBackfaceThreshold;
};


float RTXGISignNotZero(float v)
{
    return (v >= 0.0) ? 1.0 : -1.0;
}

vec2 RTXGISignNotZero2(vec2 v)
{
    return vec2(RTXGISignNotZero(v.x), RTXGISignNotZero(v.y));
}

vec2 octEncode(vec3 n) {
    float l1norm = abs(n.x) + abs(n.y) + abs(n.z);
    vec2 uv = n.xy * (1.0 / l1norm);
    if (n.z < 0.0)
    {
        uv = (1.0 - abs(uv.yx)) * RTXGISignNotZero2(uv.xy);
    }
    return uv;
}

vec3 octDecode(vec2 coords) {
    //vec2 coords = f * 2.0 - 1.0;
    vec3 direction = vec3(coords.x, coords.y, 1.0 - abs(coords.x) - abs(coords.y));
    if (direction.z < 0.0)
    {
        direction.xy = (1.0 - abs(direction.yx)) * RTXGISignNotZero2(direction.xy);
    }
    return normalize(direction);
}

/**
 * Computes a low discrepancy spherically distributed direction on the unit sphere,
 * for the given index in a set of samples. Each direction is unique in
 * the set, but the set of directions is always the same.
 */
vec3 SphericalFibonacci(uint sampleIndex, uint numSamples)
{
    const float b = (sqrt(5.0) * 0.5 + 0.5) - 1.0;
    float phi = 6.28318530718 * fract(float(sampleIndex) * b);
    float cosTheta = 1.0 - (2.0 * float(sampleIndex) + 1.0) * (1.0 / float(numSamples));
    float sinTheta = sqrt(clamp(1.0 - (cosTheta * cosTheta), 0.0, 1.0));

    return vec3((cos(phi) * sinTheta), (sin(phi) * sinTheta), cosTheta);
}

/**
 * Computes the world-space position of a probe from the probe's 3D grid-space coordinates.
 * Probe relocation is not considered.
 */
vec3 DDGIGetProbeWorldPosition(ivec3 probeCoords, ProbeVolume volume)
{
    // Multiply the grid coordinates by the probe spacing
    vec3 probeGridWorldPosition = vec3(probeCoords) * volume.spacing;

    // Shift the grid of probes by half of each axis extent to center the volume about its origin
    vec3 probeGridShift = (volume.spacing * vec3((volume.gridDimensions - 1))) * 0.5;

    // Center the probe grid about the origin
    vec3 probeWorldPosition = (probeGridWorldPosition - probeGridShift);


    // Translate the grid to the volume's center
    probeWorldPosition += volume.origin; // + (u_volume. * u_volume.spacing);

    return probeWorldPosition;
}

/**
 * Computes the 3D grid-space coordinates for the probe at the given probe index in the range [0, numProbes-1].
 * The opposite of DDGIGetProbeIndex(probeCoords,...).
 */
ivec3 DDGIGetProbeCoords(int probeIndex, ProbeVolume volume)
{
    ivec3 probeCoords;

    probeCoords.x = int(probeIndex % int(volume.gridDimensions.x));
    probeCoords.y = int(probeIndex / (int(volume.gridDimensions.x) * int(volume.gridDimensions.z)));
    probeCoords.z = int((probeIndex / int(volume.gridDimensions.x)) % int(volume.gridDimensions.z));

    return probeCoords;
}

/**
 * Computes a spherically distributed, normalized ray direction for the given ray index in a set of ray samples.
 * Applies the volume's random probe ray rotation transformation to "non-fixed" ray direction samples.
 */
vec3 DDGIGetProbeRayDirection(int rayIndex, ProbeVolume volume)
{
    int sampleIndex = rayIndex;
    int numRays = volume.probeNumRays;


    // Get a ray direction on the sphere
    vec3 direction = SphericalFibonacci(uint(sampleIndex), uint(numRays));

    // Don't rotate fixed rays so relocation/classification are temporally stable
    return normalize(direction);
}

int DDGIGetProbesPerPlane(ivec3 gridDimensions)
{
    return int(gridDimensions.x * gridDimensions.z);
}

int DDGIGetPlaneIndex(ivec3 probeCoords)
{
    return probeCoords.y;
}

int DDGIGetProbeIndexInPlane(ivec3 probeCoords, ivec3 gridDimensions)
{
    return probeCoords.x + int(gridDimensions.x * probeCoords.z);
}

/**
 * Computes normalized octahedral coordinates for the given texel coordinates.
 * Maps the top left texel to (-1,-1).
 * Used by DDGIProbeBlendingCS() in ProbeBlending.hlsl.
 */
vec2 DDGIGetNormalizedOctahedralCoordinates(ivec2 texCoords, int numTexels)
{
    // Map 2D texture coordinates to a normalized octahedral space
    vec2 octahedralTexelCoord = vec2(texCoords.x % numTexels, texCoords.y % numTexels);

    // Move to the center of a texel
    octahedralTexelCoord.xy += 0.5;

    // Normalize
    octahedralTexelCoord.xy /= float(numTexels);

    // Shift to [-1, 1);
    octahedralTexelCoord *= 2.0;
    octahedralTexelCoord -= vec2(1.0, 1.0);

    return octahedralTexelCoord;
}

/**
 * Computes the normalized octahedral direction that corresponds to the
 * given normalized coordinates on the [-1, 1] square.
 * The opposite of DDGIGetOctahedralCoordinates().
 * Used by DDGIProbeBlendingCS() in ProbeBlending.hlsl.
 */
vec3 DDGIGetOctahedralDirection(vec2 coords)
{
    vec3 direction = vec3(coords.x, coords.y, 1.0 - abs(coords.x) - abs(coords.y));
    if (direction.z < 0.0)
    {
        direction.xy = (1.0 - abs(direction.yx)) * RTXGISignNotZero2(direction.xy);
    }
    return normalize(direction);
}

int DDGIGetProbeIndex(ivec3 probeCoords, ProbeVolume volume) {
    // Get the 3D ID of the probe this workgroup is processing
    int probesPerPlane = DDGIGetProbesPerPlane(ivec3(volume.gridDimensions));
    int planeIndex = DDGIGetPlaneIndex(probeCoords);
    int probeIndexInPlane = DDGIGetProbeIndexInPlane(probeCoords, ivec3(volume.gridDimensions));

    return (planeIndex * probesPerPlane) + probeIndexInPlane;
}

/**
 * Computes the RayData Texture2DArray coordinates of the probe at the given probe index.
 *
 * When infinite scrolling is enabled, probeIndex is expected to be the scroll adjusted probe index.
 * Obtain the adjusted index with DDGIGetScrollingProbeIndex().
 */
uvec3 DDGIGetRayDataTexelCoords(int rayIndex, int probeIndex, ProbeVolume volume)
{
    int probesPerPlane = DDGIGetProbesPerPlane(ivec3(volume.gridDimensions));

    uvec3 coords;
    coords.x = uint(rayIndex);
    coords.z = uint(probeIndex / probesPerPlane);
    coords.y = uint(probeIndex - (int(coords.z) * probesPerPlane));

    return coords;
}

/**
 * Computes the surfaceBias parameter used by DDGIGetVolumeIrradiance().
 * The surfaceNormal and cameraDirection arguments are expected to be normalized.
 */
vec3 DDGIGetSurfaceBias(vec3 surfaceNormal, vec3 cameraDirection, ProbeVolume volume)
{
    return (surfaceNormal * volume.probeNormalBias) + (-cameraDirection * volume.probeViewBias);
}

/**
 * Computes the 3D grid-space coordinates of the "base" probe (i.e. floor of xyz) of the 8-probe
 * cube that surrounds the given world space position. The other seven probes of the cube
 * are offset by 0 or 1 in grid space along each axis.
 *
 * This function accounts for scroll offsets to adjust the volume's origin.
 */
ivec3 DDGIGetBaseProbeGridCoords(vec3 worldPosition, ProbeVolume volume)
{
    // Get the vector from the volume origin to the surface point
    vec3 position = worldPosition - (volume.origin);

    // Rotate the world position into the volume's space
    //if(!IsVolumeMovementScrolling(volume)) position = RTXGIQuaternionRotate(position, RTXGIQuaternionConjugate(volume.rotation));

    // Shift from [-n/2, n/2] to [0, n] (grid space)
    position += (volume.spacing * vec3(volume.gridDimensions - uvec3(1, 1, 1))) * 0.5;

    // Quantize the position to grid space
    ivec3 probeCoords = ivec3(position / volume.spacing);

    // Clamp to [0, probeCounts - 1]
    // Snaps positions outside of grid to the grid edge
    probeCoords = clamp(probeCoords, ivec3(0, 0, 0), ivec3(volume.gridDimensions - uvec3(1, 1, 1)));

    return probeCoords;
}

/**
 * Computes the Texture2DArray coordinates of the probe at the given probe index.
 *
 * When infinite scrolling is enabled, probeIndex is expected to be the scroll adjusted probe index.
 * Obtain the adjusted index with DDGIGetScrollingProbeIndex().
 */
uvec3 DDGIGetProbeTexelCoords(int probeIndex, ProbeVolume volume)
{
    // Find the probe's plane index
    int probesPerPlane = DDGIGetProbesPerPlane(ivec3(volume.gridDimensions));
    int planeIndex = int(probeIndex / probesPerPlane);

    int x = (probeIndex % int(volume.gridDimensions.x));
    int y = (probeIndex / int(volume.gridDimensions.x)) % int(volume.gridDimensions.z);

    return uvec3(uint(x), uint(y), uint(planeIndex));
}

/**
 * Computes the normalized texture UVs within the Probe Irradiance and Probe Distance texture arrays
 * given the probe index and 2D normalized octant coordinates [-1, 1]. Used when sampling the texture arrays.
 * 
 * When infinite scrolling is enabled, probeIndex is expected to be the scroll adjusted probe index.
 * Obtain the adjusted index with DDGIGetScrollingProbeIndex().
 */
vec3 DDGIGetProbeUV(int probeIndex, vec2 octantCoordinates, int numProbeInteriorTexels, ProbeVolume volume)
{
    // Get the probe's texel coordinates, assuming one texel per probe
    uvec3 coords = DDGIGetProbeTexelCoords(probeIndex, volume);

    // Add the border texels to get the total texels per probe
    float numProbeTexels = float(numProbeInteriorTexels + 2);

    float textureWidth = numProbeTexels * float(volume.gridDimensions.x);
    float textureHeight = numProbeTexels * float(volume.gridDimensions.z);

    // Move to the center of the probe and move to the octant texel before normalizing
    vec2 uv = vec2(float(coords.x) * numProbeTexels, float(coords.y) * numProbeTexels) + (numProbeTexels * 0.5);
    uv += octantCoordinates.xy * (float(numProbeInteriorTexels) * 0.5);
    uv /= vec2(textureWidth, textureHeight);
    return vec3(uv, float(coords.z));
}

/**
 * Computes the octant coordinates in the normalized [-1, 1] square, for the given a unit direction vector.
 * The opposite of DDGIGetOctahedralDirection().
 * Used by GetDDGIVolumeIrradiance() in Irradiance.hlsl.
 */
vec2 DDGIGetOctahedralCoordinates(vec3 direction)
{
    float l1norm = abs(direction.x) + abs(direction.y) + abs(direction.z);
    vec2 uv = direction.xy * (1.0 / l1norm);
    if (direction.z < 0.0)
    {
        uv = (1.0 - abs(uv.yx)) * RTXGISignNotZero2(uv.xy);
    }
    return uv;
}

///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////

// THE RayData NEEDS TO BE DEFINED IN THE GLOBAL SCOPE OF THE FILE THAT INCLUDES IT

#ifdef RAY_DATA_TEXTURE

    // sample skybox and store
    void DDGIStoreProbeRayMiss(ivec3 coords, vec3 sunColor)
    {
        imageStore(RayData, coords, vec4(sunColor, 100000000.0));

    }

    void DDGIStoreProbeRayFrontfaceHit(ivec3 coords, vec3 radiance, float hitT)
    {
        imageStore(RayData, coords, vec4(radiance, hitT));

    }

    void DDGIStoreProbeRayBackfaceHit(ivec3 coords, float hitT)
    {
        float backfaceVisibility = -hitT * 0.2;
        imageStore(RayData, coords, vec4(0.0, 0.0, 0.0, backfaceVisibility));

    }
#endif

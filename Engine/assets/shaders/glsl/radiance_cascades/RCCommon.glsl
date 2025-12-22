#ifndef RC_COMMON_GLSL
#define RC_COMMON_GLSL

#define RC_MAX_CASCADES 8

struct RCCascade {
    vec3 origin;
    float minRange;

    vec3 spacing;
    float maxRange;

    uvec3 gridSize;
    uint angularResolution;

    uint raysPerProbe;
    uint cascadeIndex;
    uint totalProbes;
    uint _padding;
};

struct RCVolume {
    vec3 volumeOrigin;
    uint numCascades;

    vec4 rotation;

    float rangeExp;
    float spacingMultiplier;
    float maxRayDistance;
    uint angularResolutionExp;

    RCCascade cascades[RC_MAX_CASCADES];
};

vec3 RCGetProbeWorldPosition(ivec3 probeCoords, RCCascade cascade) {
    vec3 probeGridWorldPosition = vec3(probeCoords) * cascade.spacing;
    vec3 probeGridShift = (cascade.spacing * vec3(cascade.gridSize - 1u)) * 0.5;
    vec3 probeWorldPosition = (probeGridWorldPosition - probeGridShift) + cascade.origin;
    return probeWorldPosition;
}

ivec3 RCGetProbeCoords(int probeIndex, RCCascade cascade) {
    ivec3 probeCoords;
    probeCoords.x = probeIndex % int(cascade.gridSize.x);
    probeCoords.y = probeIndex / (int(cascade.gridSize.x) * int(cascade.gridSize.z));
    probeCoords.z = (probeIndex / int(cascade.gridSize.x)) % int(cascade.gridSize.z);
    return probeCoords;
}

int RCGetProbeIndex(ivec3 probeCoords, RCCascade cascade) {
    int probesPerPlane = int(cascade.gridSize.x * cascade.gridSize.z);
    int planeIndex = probeCoords.y;
    int probeIndexInPlane = probeCoords.x + int(cascade.gridSize.x) * probeCoords.z;
    return (planeIndex * probesPerPlane) + probeIndexInPlane;
}

vec3 SphericalFibonacci(uint sampleIndex, uint numSamples) {
    const float PHI = (sqrt(5.0) * 0.5 + 0.5) - 1.0;
    float phi = 6.28318530718 * fract(float(sampleIndex) * PHI);
    float cosTheta = 1.0 - (2.0 * float(sampleIndex) + 1.0) * (1.0 / float(numSamples));
    float sinTheta = sqrt(clamp(1.0 - cosTheta * cosTheta, 0.0, 1.0));
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

vec3 RCGetRayDirection(int rayIndex, RCCascade cascade) {
    return SphericalFibonacci(uint(rayIndex), cascade.raysPerProbe);
}

uvec3 RCGetRayDataTexelCoords(int rayIndex, ivec3 probeCoords, RCCascade cascade) {
    uint angRes = cascade.angularResolution;
    uint rayX = uint(rayIndex) % angRes;
    uint rayY = (uint(rayIndex) / angRes) % angRes;
    uint rayZ = uint(rayIndex) / (angRes * angRes);

    uvec3 coords;
    coords.x = uint(probeCoords.x) * angRes + rayX;
    coords.y = uint(probeCoords.z) * angRes + rayY;
    coords.z = uint(probeCoords.y) * angRes + rayZ;
    return coords;
}

int RCGetProbesPerPlane(RCCascade cascade) {
    return int(cascade.gridSize.x * cascade.gridSize.z);
}

#endif


// Cascade level info structure
struct CascadeLevelInfo {
    uint cascadeLevel;
    
    ivec3 probeGridDimensions;
    vec3 probeSpacing;
    vec3 probeOrigin;

    float minProbeDistance;
    float maxProbeDistance;

    uint angularResolution;

    uint cascadeTextureIndex;
};

vec3 GetProbeWorldPosition(ivec3 probeCoords, CascadeLevelInfo cascadeLevelInfo)
{
    // Multiply the grid coordinates by the probe spacing
    vec3 probeGridWorldPosition = vec3(probeCoords) * cascadeLevelInfo.probeSpacing;

    // Shift the grid of probes by half of each axis extent to center the volume about its origin
    vec3 probeGridShift = (cascadeLevelInfo.probeSpacing * vec3((cascadeLevelInfo.probeGridDimensions - 1))) * 0.5;

    // Center the probe grid about the origin
    vec3 probeWorldPosition = (probeGridWorldPosition - probeGridShift);

    probeWorldPosition += cascadeLevelInfo.probeOrigin;

    return probeWorldPosition;
}

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

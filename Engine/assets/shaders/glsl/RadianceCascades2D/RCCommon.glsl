
// Cascade level info structure
struct CascadeLevelInfo {
    uint cascadeLevel;
    
    ivec2 probeGridDimensions;
    vec2 probeSpacing;
    vec2 probeOrigin;

    float minProbeDistance;
    float maxProbeDistance;

    uint angularResolution;

    uint cascadeTextureIndex;
    uint irradianceTextureIndex;
};

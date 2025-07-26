#pragma once

#include <glm/glm.hpp>

namespace Rapture {


struct BuildParams2D {


    // First cascade covers [0, baseRange]
    float baseRange = 2.0f;  // t_0 (tune based on scene)
    float rangeExp = 2.0f;
     // Base grid dimensions (P_0)
    glm::ivec2 baseGridDimensions = glm::ivec2(256, 256); // Must be power-of-2 friendly

    // Base angular resolution (Q_0 dimension)
    int baseAngularResolution = 4;  // For N for NxN map
    
    // Base probe spacing (∆p_0)
    float baseSpacing = 0.1f;  // Must satisfy ∆p_0 < t_0

};

struct RadianceCascadeLevel2D {
    alignas(4) uint32_t cascadeLevel = UINT32_MAX;

    alignas(8) glm::ivec2 probeGridDimensions = glm::ivec2(0);

    alignas(8) glm::vec2 probeSpacing = glm::vec2(0.0f); // 
    alignas(8) glm::vec2 probeOrigin = glm::vec2(0.0f);

    alignas(4) float minProbeDistance = 0.0f;
    alignas(4) float maxProbeDistance = 0.0f;
    
    alignas(4) uint32_t angularResolution = 0; // NxN = number of rays  

    alignas(4) uint32_t cascadeTextureIndex = UINT32_MAX; // bindless index of the cascade
    alignas(4) uint32_t irradianceTextureIndex = UINT32_MAX; // bindless index of the irradiance texture


};

struct RCProbeTracePushConstants2D {
    uint32_t cascadeIndex;
    uint32_t cascadeLevels;
    uint32_t tlasIndex;
    uint32_t lightCount;
    uint32_t skyboxTextureIndex;
};

}
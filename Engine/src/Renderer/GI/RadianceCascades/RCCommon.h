#pragma once

#include <glm/glm.hpp>

namespace Rapture {


struct BuildParams {


    // First cascade covers [0, baseRange]
    float baseRange = 4.0f;  // t_0 (tune based on scene)

     // Base grid dimensions (P_0)
    glm::ivec3 baseGridDimensions = glm::ivec3(32, 32, 32); // Must be power-of-2 friendly

    // Base angular resolution (Q_0 dimension)
    int baseAngularResolution = 8;  // For N for NxN map
    
    // Base probe spacing (∆p_0)
    float baseSpacing = 1.0f;  // Must satisfy ∆p_0 < t_0
};

struct RadianceCascadeLevel {
    alignas(4) uint32_t cascadeLevel = UINT32_MAX;

    alignas(16) glm::ivec3 probeGridDimensions = glm::ivec3(0);

    alignas(16) glm::vec3 probeSpacing = glm::vec3(0.0f); // 
    alignas(16) glm::vec3 probeOrigin = glm::vec3(0.0f);

    alignas(4) float minProbeDistance = 0.0f;
    alignas(4) float maxProbeDistance = 0.0f;
    
    alignas(4) uint32_t angularResolution = 0; // NxN = number of rays  

    alignas(4) uint32_t cascadeTextureIndex = UINT32_MAX; // bindless index of the cascade


};

struct RCProbeTracePushConstants {
    uint32_t cascadeIndex;
    uint32_t cascadeLevels;
    uint32_t tlasIndex;
    uint32_t lightCount;
    uint32_t skyboxTextureIndex;
};

}
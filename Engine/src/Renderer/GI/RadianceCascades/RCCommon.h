#pragma once

#include <glm/glm.hpp>

namespace Rapture {


struct BuildParams {

    int numCascades = 8;

    /**
     * @brief The range extent of the first cascade (t_1, since t_0 is 0)
     */
    float baseRange = 2.0f;

    /**
     * @brief The exponential factor used to determine subsequent cascade ranges (t_i ~ pow(rangeScaleFactor, i)).
     * Must be > 1. Typically 2.0.
     */
    float rangeScaleFactor = 2.0f;

    /**
     * @brief The factor by which spatial resolution changes per cascade (e.g., 0.5 means dimensions halve each step).
     * This affects probe spacing (Δp ~ 1 / gridScaleFactor^i). Should be < 1.
     * Based on paper scaling Δp ~ 2^i, if t_i ~ 2^i, this implies probe spacing doubles, so grid dimensions should halve.
     */
    float gridScaleFactor = 0.5f;

    /**
     * @brief The spatial grid resolution of the first cascade (P_0)
     */
    glm::ivec3 baseGridDimensions = glm::ivec3(32, 32, 32); // Example dimensions
    glm::vec3 baseGridSpacing = glm::vec3(1.0, 1.0, 1.0); // Example dimensions

    /**
     * @brief The angular resolution 'dimension' of the first cascade (Q_0).
     * E.g., for an NxN octahedral map, this would be N.
     */
    int baseAngularResolution = 8; // Example angular resolution

   /**
    * @brief The factor by which angular resolution changes per cascade.
    * Based on paper scaling Δω ~ 1/2^i, angular resolution should double.
    * Must be > 1. Typically 2.0.
    */
   float angularScaleFactor = 2.0f;


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
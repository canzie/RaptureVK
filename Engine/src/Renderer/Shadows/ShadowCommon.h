#pragma once


#include <glm/glm.hpp>
#include <cstdint>


#define MAX_CASCADES 4u
#define MAX_SHADOW_CASTERS 16u

namespace Rapture {

    // gets used in the ubo for drawing to the shadowmap textures
    struct ShadowMapData {
        alignas(16) glm::mat4 lightViewProjection;
    };

    struct CSMData {
        alignas(16) glm::mat4 lightViewProjection[MAX_CASCADES];
    };

    // Aligned for std430 layout
    struct alignas(16) ShadowBufferData {
        alignas(4) int type;           
        alignas(4) uint32_t cascadeCount; 
        alignas(4) uint32_t lightIndex;   
        alignas(4) uint32_t textureHandle;

        alignas(16) glm::mat4 cascadeMatrices[MAX_CASCADES];

        alignas(16) glm::vec4 cascadeSplitsViewSpace[MAX_CASCADES];
    };


    // Aligned for std430 layout
    struct alignas(16) ShadowStorageLayout {
        alignas(4) uint32_t shadowCount; 

        alignas(16) ShadowBufferData shadowData[MAX_SHADOW_CASTERS];
    };

}

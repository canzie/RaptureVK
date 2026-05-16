#ifndef RAPTURE__GPUDATASTRUCTS_H
#define RAPTURE__GPUDATASTRUCTS_H

#include <cstdint>
#include <glm/glm.hpp>

namespace Rapture {

/**
 * @brief Per-mesh data for the mesh SSBO (std430 layout)
 */
struct alignas(16) MeshGPUData {
    alignas(16) glm::mat4 modelMatrix;
    alignas(4) uint32_t materialIndex;
    alignas(4) uint32_t vertexBufferFlags;
    alignas(4) uint32_t entityId;
};

/**
 * @brief Per-light data for the light SSBO (std430 layout)
 */
struct alignas(16) LightGPUData {
    alignas(16) glm::vec4 positionAndType;   ///< xyz = world position, w = LightType as float
    alignas(16) glm::vec4 directionAndRange; ///< xyz = direction, w = attenuation range
    alignas(16) glm::vec4 colorAndIntensity; ///< xyz = RGB color, w = intensity multiplier
    alignas(16) glm::vec4 spotAngles;        ///< x = inner cone cos, y = outer cone cos, z = entity id, w = unused
};

/**
 * @brief Per-camera data for the camera SSBO (std430 layout)
 */
struct alignas(16) CameraGPUData {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 projection;
};

#define MAX_CASCADES 4u

/**
 * @brief Per-shadow data for the shadow SSBO (std430 layout)
 */
struct alignas(16) ShadowGPUData {
    alignas(4) int type; ///< 0 = point, 1 = directional, 2 = spot
    alignas(4) uint32_t cascadeCount;
    alignas(4) uint32_t lightIndex;
    alignas(4) uint32_t textureHandle;
    alignas(16) glm::mat4 cascadeMatrices[MAX_CASCADES];
    alignas(16) glm::vec4 cascadeSplitsViewSpace[MAX_CASCADES];
};

} // namespace Rapture

#endif // RAPTURE__GPUDATASTRUCTS_H

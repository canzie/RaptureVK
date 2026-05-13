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
    alignas(4) uint32_t _pad;
};

/**
 * @brief Per-light data for the light SSBO (std430 layout)
 */
struct alignas(16) LightGPUData {
    alignas(16) glm::vec4 positionAndRange;  ///< xyz = world position, w = attenuation range
    alignas(16) glm::vec4 directionAndType;  ///< xyz = direction, w = LightType as float
    alignas(16) glm::vec4 colorAndIntensity; ///< xyz = RGB color, w = intensity multiplier
    alignas(4) float innerConeCos;           ///< cos(inner cone angle), spot lights only
    alignas(4) float outerConeCos;           ///< cos(outer cone angle), spot lights only
    alignas(4) uint32_t entityId;
    alignas(4) uint32_t _pad;
};

/**
 * @brief Per-camera data for the camera SSBO (std430 layout)
 */
struct alignas(16) CameraGPUData {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 projection;
    alignas(16) glm::mat4 viewProjection;
    alignas(16) glm::vec4 positionAndNear; ///< xyz = world position, w = near plane
    alignas(16) glm::vec4 forwardAndFar;   ///< xyz = forward direction, w = far plane
};

} // namespace Rapture

#endif // RAPTURE__GPUDATASTRUCTS_H

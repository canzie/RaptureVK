#ifndef RAPTURE__RC_COMMON_H
#define RAPTURE__RC_COMMON_H

#include <cstdint>
#include <glm/glm.hpp>

#include "Logging/Log.h"

namespace Rapture {

constexpr uint32_t RC_MAX_CASCADES = 8;

struct RadianceCascadeConfig {
    glm::vec3 origin = glm::vec3(0.0f);
    glm::vec3 baseProbeSpacing = glm::vec3(1.0f);
    glm::uvec3 baseGridSize = glm::uvec3(32);

    uint32_t numCascades = 4;
    float rangeMultiplier = 2.0f;
    float spacingMultiplier = 2.0f;
    uint32_t angularResolutionMultiplier = 2;

    float baseRange = 1.0f;
    uint32_t baseAngularResolution = 4;

    float maxRayDistance = 1000.0f;

    float getCascadeRange(uint32_t cascade) const
    {
        float range = baseRange;
        for (uint32_t i = 0; i < cascade; ++i) {
            range *= rangeMultiplier;
        }
        return glm::min(range, maxRayDistance);
    }

    float getCascadeMinRange(uint32_t cascade) const
    {
        if (cascade == 0) return 0.0f;
        return getCascadeRange(cascade - 1);
    }

    glm::vec3 getCascadeSpacing(uint32_t cascade) const
    {
        glm::vec3 spacing = baseProbeSpacing;
        for (uint32_t i = 0; i < cascade; ++i) {
            spacing *= spacingMultiplier;
        }
        return spacing;
    }

    glm::uvec3 getCascadeGridSize(uint32_t cascade) const
    {
        glm::uvec3 gridSize = baseGridSize;
        for (uint32_t i = 0; i < cascade; ++i) {
            gridSize = glm::max(gridSize / 2u, glm::uvec3(1));
        }
        return gridSize;
    }

    uint32_t getCascadeAngularResolution(uint32_t cascade) const
    {
        uint32_t res = baseAngularResolution;
        for (uint32_t i = 0; i < cascade; ++i) {
            res *= angularResolutionMultiplier;
        }
        return res;
    }

    uint32_t getCascadeRaysPerProbe(uint32_t cascade) const
    {
        uint32_t res = getCascadeAngularResolution(cascade);
        return res * res * res;
    }
};

// GPU struct - must match GLSL layout
struct alignas(16) RCCascadeGPU {
    alignas(16) glm::vec3 origin;
    alignas(4) float minRange;

    alignas(16) glm::vec3 spacing;
    alignas(4) float maxRange;

    alignas(16) glm::uvec3 gridSize;
    alignas(4) uint32_t angularResolution;

    alignas(4) uint32_t raysPerProbe;
    alignas(4) uint32_t cascadeIndex;
    alignas(4) uint32_t totalProbes;
    alignas(4) uint32_t _padding;
};

struct alignas(16) RCVolumeGPU {
    alignas(16) glm::vec3 volumeOrigin;
    alignas(4) uint32_t numCascades;

    alignas(16) glm::vec4 rotation;

    alignas(4) float rangeMultiplier;
    alignas(4) float spacingMultiplier;
    alignas(4) float maxRayDistance;
    alignas(4) uint32_t _padding;

    RCCascadeGPU cascades[RC_MAX_CASCADES];
};

inline RCVolumeGPU buildRCVolumeGPU(const RadianceCascadeConfig &config)
{
    RCVolumeGPU gpu = {};

    gpu.volumeOrigin = config.origin;
    gpu.numCascades = config.numCascades;
    gpu.rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    gpu.rangeMultiplier = config.rangeMultiplier;
    gpu.spacingMultiplier = config.spacingMultiplier;
    gpu.maxRayDistance = config.maxRayDistance;

    for (uint32_t i = 0; i < config.numCascades && i < RC_MAX_CASCADES; ++i) {
        RCCascadeGPU &cascade = gpu.cascades[i];

        cascade.origin = config.origin;
        cascade.minRange = config.getCascadeMinRange(i);
        cascade.maxRange = config.getCascadeRange(i);
        cascade.spacing = config.getCascadeSpacing(i);
        cascade.gridSize = config.getCascadeGridSize(i);
        cascade.angularResolution = config.getCascadeAngularResolution(i);
        cascade.raysPerProbe = config.getCascadeRaysPerProbe(i);
        cascade.cascadeIndex = i;
        cascade.totalProbes = cascade.gridSize.x * cascade.gridSize.y * cascade.gridSize.z;
    }

    return gpu;
}

inline void PrintRcVolumeGPU(const RCVolumeGPU &volume)
{
    RP_CORE_INFO("--- RCVolumeGPU ---");
    RP_CORE_INFO("Origin: ({}, {}, {})", volume.volumeOrigin.x, volume.volumeOrigin.y, volume.volumeOrigin.z);
    RP_CORE_INFO("Num Cascades: {}", volume.numCascades);
    RP_CORE_INFO("Rotation: ({}, {}, {}, {})", volume.rotation.x, volume.rotation.y, volume.rotation.z, volume.rotation.w);
    RP_CORE_INFO("Range Multiplier: {}", volume.rangeMultiplier);
    RP_CORE_INFO("Spacing Multiplier: {}", volume.spacingMultiplier);
    RP_CORE_INFO("Max Ray Distance: {}", volume.maxRayDistance);

    for (uint32_t i = 0; i < volume.numCascades; ++i) {
        const RCCascadeGPU &cascade = volume.cascades[i];
        RP_CORE_INFO("\t--- Cascade {} ---", i);
        RP_CORE_INFO("\tOrigin: ({}, {}, {})", cascade.origin.x, cascade.origin.y, cascade.origin.z);
        RP_CORE_INFO("\tMin Range: {}", cascade.minRange);
        RP_CORE_INFO("\tMax Range: {}", cascade.maxRange);
        RP_CORE_INFO("\tSpacing: ({}, {}, {})", cascade.spacing.x, cascade.spacing.y, cascade.spacing.z);
        RP_CORE_INFO("\tGrid Size: ({}, {}, {})", cascade.gridSize.x, cascade.gridSize.y, cascade.gridSize.z);
        RP_CORE_INFO("\tAngular Resolution: {}", cascade.angularResolution);
        RP_CORE_INFO("\tRays Per Probe: {}", cascade.raysPerProbe);
        RP_CORE_INFO("\tTotal Probes: {}", cascade.totalProbes);
    }
    RP_CORE_INFO("-------------------");
}

} // namespace Rapture

#endif // RAPTURE__RC_COMMON_H

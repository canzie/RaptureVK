#pragma once

#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

#include "Logging/Log.h"

namespace Rapture {

enum QueueFamilyIndex {
    GRAPHICS = 0,
    COMPUTE = 1,
    PRESENT = 2,
    TRANSFER = 3,
    COUNT,
};

struct QueueFamilyIndices {
    uint32_t familyIndices[COUNT] = {UINT32_MAX};
    uint32_t familyQueueCounts[COUNT] = {0};

    bool isComplete() const
    {
        return familyIndices[GRAPHICS] != UINT32_MAX && familyIndices[COMPUTE] != UINT32_MAX &&
               familyIndices[PRESENT] != UINT32_MAX;
    }

    void print() const
    {
        RP_CORE_INFO("Queue family indices:");
        RP_CORE_INFO("Graphics: {}:{}", familyIndices[GRAPHICS], familyQueueCounts[GRAPHICS]);
        RP_CORE_INFO("Compute: {}:{}", familyIndices[COMPUTE], familyQueueCounts[COMPUTE]);
        RP_CORE_INFO("Present: {}:{}", familyIndices[PRESENT], familyQueueCounts[PRESENT]);
        RP_CORE_INFO("Transfer: {}:{}", familyIndices[TRANSFER], familyQueueCounts[TRANSFER]);
    }
}; // namespace Rapture

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

} // namespace Rapture

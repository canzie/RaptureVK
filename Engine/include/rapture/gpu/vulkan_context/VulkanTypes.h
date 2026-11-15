#ifndef RAPTURE__VULKAN_TYPES_H
#define RAPTURE__VULKAN_TYPES_H

#include <algorithm>
#include <iterator>
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

#include <rapture/core/utils/Log.h>

namespace Rapture {

enum QueueFamilyIndex {
    GRAPHICS = 0,
    COMPUTE = 1,
    PRESENT = 2,
    TRANSFER = 3,
    COUNT,
};

/**
 * @brief How the graphics, compute and transfer roles are spread over the device's queue families
 */
enum class QueueFamilyAssignment {
    ONE_FAMILY,
    DEDICATED_FAMILIES
};

struct QueueFamilyIndices {
    QueueFamilyIndices() { std::fill(std::begin(familyIndices), std::end(familyIndices), UINT32_MAX); }

    uint32_t familyIndices[COUNT];
    uint32_t familyQueueCounts[COUNT] = {0};

    /**
     * @brief Whether every family the device needs has been found
     * @param presentRequired Whether the device presents to a surface
     * @return True if graphics, compute and, when required, present have a family
     */
    bool isComplete(bool presentRequired) const
    {
        return familyIndices[GRAPHICS] != UINT32_MAX && familyIndices[COMPUTE] != UINT32_MAX &&
               (!presentRequired || familyIndices[PRESENT] != UINT32_MAX);
    }

    void print() const
    {
        RP_CORE_INFO("Queue family indices:");
        RP_CORE_INFO("Graphics: {}:{}", familyIndices[GRAPHICS], familyQueueCounts[GRAPHICS]);
        RP_CORE_INFO("Compute: {}:{}", familyIndices[COMPUTE], familyQueueCounts[COMPUTE]);
        RP_CORE_INFO("Present: {}:{}", familyIndices[PRESENT], familyQueueCounts[PRESENT]);
        RP_CORE_INFO("Transfer: {}:{}", familyIndices[TRANSFER], familyQueueCounts[TRANSFER]);
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

} // namespace Rapture

#endif // RAPTURE__VULKAN_TYPES_H

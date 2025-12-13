#pragma once

#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

namespace Rapture {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> computeFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> transferFamily;
    uint32_t graphicsFamilyQueueCount = 1;

    bool isComplete() const { return graphicsFamily.has_value() && computeFamily.has_value() && presentFamily.has_value(); }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

} // namespace Rapture

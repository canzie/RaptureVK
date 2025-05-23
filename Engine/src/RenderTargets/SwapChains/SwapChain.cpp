#include "SwapChain.h"

#include "WindowContext/Application.h"
#include "Logging/Log.h"

#include <algorithm>
#include <stdexcept>

namespace Rapture {
    SwapChain::SwapChain()
    : m_swapChain(VK_NULL_HANDLE), 
    m_swapChainImages(),
    m_swapChainImageViews(), 
    m_swapChainImageFormat(),
    m_swapChainExtent(), 
    m_imageCount(0) {

        //invalidate();

    }


SwapChain::~SwapChain() {
    destroy();
}

void SwapChain::recreate() {
    invalidate();
}

void SwapChain::destroy() {
    if (m_swapChain == VK_NULL_HANDLE) {
        return;
    }

    auto& app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    for (size_t i = 0; i < m_swapChainImageViews.size(); i++) {
        if (m_swapChainImageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, m_swapChainImageViews[i], nullptr);
        }
    }

    vkDestroySwapchainKHR(device, m_swapChain, nullptr);

    m_swapChain = VK_NULL_HANDLE;
    m_swapChainImageViews.clear();
    m_swapChainImages.clear();
    m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    m_swapChainExtent = {};
    m_imageCount = 0;
}

void SwapChain::invalidate() {

    auto& app = Application::getInstance();
    auto surface = app.getVulkanContext().getSurface();
    auto physicalDevice = app.getVulkanContext().getPhysicalDevice();
    auto& windowContext = app.getWindowContext();
    auto device = app.getVulkanContext().getLogicalDevice();

    if (device == VK_NULL_HANDLE) {
        RP_CORE_ERROR("SwapChain::invalidate - logical device is nullptr!");
        throw std::runtime_error("SwapChain::invalidate - logical device is nullptr!");
    }

    for (size_t i = 0; i < m_swapChainImageViews.size(); i++) {
        if (m_swapChainImageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, m_swapChainImageViews[i], nullptr);
        }
    } 

    if (m_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, m_swapChain, nullptr);
    }

    SwapChainSupportDetails2 swapChainSupport = querySwapChainSupport(physicalDevice, surface);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities, windowContext);

    // Calculate desired image count, ensuring it's within valid bounds
    uint32_t desiredImageCount = swapChainSupport.capabilities.minImageCount + 1;
    m_imageCount = std::min<uint32_t>(desiredImageCount, swapChainSupport.capabilities.maxImageCount);


    auto indices = app.getVulkanContext().getQueueFamilyIndices();
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};


    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = m_imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_swapChain) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to create swap chain!");
        throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(device, m_swapChain, &m_imageCount, nullptr);
    
    m_swapChainImages.resize(m_imageCount);
    vkGetSwapchainImagesKHR(device, m_swapChain, &m_imageCount, m_swapChainImages.data());


    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent = extent;

    createImageViews();

}

void SwapChain::createImageViews() {
    auto& app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();  

    m_swapChainImageViews.resize(m_swapChainImages.size());


    for (size_t i = 0; i < m_swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_swapChainImages[i];

        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_swapChainImageFormat;

        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &createInfo, nullptr, &m_swapChainImageViews[i]) != VK_SUCCESS) {
            RP_CORE_ERROR("SwapChain::createImageViews - failed to create image views!");
            throw std::runtime_error("SwapChain::createImageViews - failed to create image views!");
        }


    }

}

SwapChainSupportDetails2 SwapChain::querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    SwapChainSupportDetails2 details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}


VkSurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR SwapChain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities, const WindowContext& windowContext) {
    if (capabilities.currentExtent.width != (std::numeric_limits<uint32_t>::max)()) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        windowContext.getFramebufferSize(&width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

}

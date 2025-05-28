#include "SwapChain.h"

#include "WindowContext/Application.h"
#include "Logging/Log.h"

#include <algorithm>
#include <stdexcept>

namespace Rapture {

    RenderMode SwapChain::renderMode = RenderMode::PRESENTATION;

    SwapChain::SwapChain(VkDevice device, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, QueueFamilyIndices queueFamilyIndices, WindowContext* windowContext)
        : m_device(device)
        , m_surface(surface)
        , m_physicalDevice(physicalDevice)
        , m_queueFamilyIndices(queueFamilyIndices)
        , m_windowContext(windowContext)
    {
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


    for (size_t i = 0; i < m_swapChainImageViews.size(); i++) {
        if (m_swapChainImageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, m_swapChainImageViews[i], nullptr);
        }
    }
    if (m_depthTexture) {
        m_depthTexture.reset();
    }

    vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);

    m_swapChain = VK_NULL_HANDLE;
    m_swapChainImageViews.clear();
    m_swapChainImages.clear();
    m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    m_swapChainExtent = {};
    m_imageCount = 0;
}

void SwapChain::invalidate() {

    if (m_device == VK_NULL_HANDLE) {
        RP_CORE_ERROR("SwapChain::invalidate - logical device is nullptr!");
        throw std::runtime_error("SwapChain::invalidate - logical device is nullptr!");
    }

    for (size_t i = 0; i < m_swapChainImageViews.size(); i++) {
        if (m_swapChainImageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, m_swapChainImageViews[i], nullptr);
        }
    } 

    if (m_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
    }

    SwapChainSupportDetails2 swapChainSupport = querySwapChainSupport();

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    // Calculate desired image count, ensuring it's within valid bounds
    uint32_t desiredImageCount = swapChainSupport.capabilities.minImageCount + 1;
    m_imageCount = std::min<uint32_t>(desiredImageCount, swapChainSupport.capabilities.maxImageCount);

    uint32_t queueFamilyIndices[] = {m_queueFamilyIndices.graphicsFamily.value(), m_queueFamilyIndices.presentFamily.value()};


    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = m_imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;

if (renderMode == RenderMode::PRESENTATION) {
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
} else {
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
 }

    if (m_queueFamilyIndices.graphicsFamily != m_queueFamilyIndices.presentFamily) {
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

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to create swap chain!");
        throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(m_device, m_swapChain, &m_imageCount, nullptr);
    
    m_swapChainImages.resize(m_imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &m_imageCount, m_swapChainImages.data());


    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent = extent;

    createImageViews();
    createDepthTexture();

}

void SwapChain::createImageViews() {

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

        if (vkCreateImageView(m_device, &createInfo, nullptr, &m_swapChainImageViews[i]) != VK_SUCCESS) {
            RP_CORE_ERROR("SwapChain::createImageViews - failed to create image views!");
            throw std::runtime_error("SwapChain::createImageViews - failed to create image views!");
        }


    }

}

void SwapChain::createDepthTexture() {
    TextureSpecification depthSpec{};
    depthSpec.type = TextureType::TEXTURE2D;
    depthSpec.format = TextureFormat::D32F;
    depthSpec.width = m_swapChainExtent.width;
    depthSpec.height = m_swapChainExtent.height;
    depthSpec.depth = 1;
    depthSpec.mipLevels = 1;
    depthSpec.srgb = false; // Depth textures don't use sRGB
    
    m_depthTexture = std::make_shared<Texture>(depthSpec);
    
    RP_CORE_INFO("Created depth texture: {}x{}", depthSpec.width, depthSpec.height);
}

SwapChainSupportDetails2 SwapChain::querySwapChainSupport()
{
    SwapChainSupportDetails2 details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, details.presentModes.data());
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

VkExtent2D SwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) {
    if (capabilities.currentExtent.width != (std::numeric_limits<uint32_t>::max)()) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        m_windowContext->getFramebufferSize(&width, &height);

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

#pragma once

#include "WindowContext/WindowContext.h"
#include "vulkan/vulkan.h"

namespace Rapture {

    struct SwapChainSupportDetails2 {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };


    class SwapChain {
    public:
        SwapChain();
        ~SwapChain();

        void recreate();

        void destroy();

        VkExtent2D getExtent() const { return m_swapChainExtent; }
        VkFormat getImageFormat() const { return m_swapChainImageFormat; }
        std::vector<VkImageView> getImageViews() const { return m_swapChainImageViews; }
        VkSwapchainKHR getSwapChainVk() const { return m_swapChain; }
        uint32_t getImageCount() const { return m_imageCount; }
        
        void invalidate();

    private:

        void createImageViews();


        SwapChainSupportDetails2 querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, const WindowContext& windowContext);

    private:
        VkSwapchainKHR m_swapChain;
        std::vector<VkImage> m_swapChainImages;
        std::vector<VkImageView> m_swapChainImageViews;
        VkFormat m_swapChainImageFormat;
        VkExtent2D m_swapChainExtent;

        uint32_t m_imageCount;
};


}


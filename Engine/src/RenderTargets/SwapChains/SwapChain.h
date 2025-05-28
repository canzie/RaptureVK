#pragma once

#include "WindowContext/WindowContext.h"
#include "WindowContext/VulkanContext/VulkanTypes.h"
#include "vulkan/vulkan.h"
#include "Textures/Texture.h"
#include <memory>

namespace Rapture {

    struct SwapChainSupportDetails2 {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    enum class RenderMode {
        PRESENTATION,
        OFFSCREEN
    };

    class SwapChain {
    public:
        SwapChain(VkDevice device, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, QueueFamilyIndices queueFamilyIndices, WindowContext* windowContext);
        ~SwapChain();

        void recreate();
        void destroy();

        VkExtent2D getExtent() const { return m_swapChainExtent; }
        VkFormat getImageFormat() const { return m_swapChainImageFormat; }
        std::vector<VkImageView> getImageViews() const { return m_swapChainImageViews; }
        std::vector<VkImage> getImages() const { return m_swapChainImages; }
        VkSwapchainKHR getSwapChainVk() const { return m_swapChain; }
        uint32_t getImageCount() const { return m_imageCount; }
        std::shared_ptr<Texture> getDepthTexture() const { return m_depthTexture; }
        VkFormat getDepthImageFormat() const { return VK_FORMAT_D32_SFLOAT; }
        
        void invalidate();

        static RenderMode renderMode;

    private:
        void createImageViews();
        void createDepthTexture();

        SwapChainSupportDetails2 querySwapChainSupport();
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    private:
        VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
        std::vector<VkImage> m_swapChainImages;
        std::vector<VkImageView> m_swapChainImageViews;
        VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D m_swapChainExtent{};

        uint32_t m_imageCount = 0;
        
        std::shared_ptr<Texture> m_depthTexture;

        VkDevice m_device = VK_NULL_HANDLE;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        QueueFamilyIndices m_queueFamilyIndices{};

        WindowContext* m_windowContext = nullptr;
    };

}


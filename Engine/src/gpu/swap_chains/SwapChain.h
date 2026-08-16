#pragma once

#include "core/events/EventSignal.h"
#include "gpu/textures/Texture.h"
#include "vulkan/vulkan.h"
#include "platform/WindowContext.h"
#include "gpu/vulkan_context/VulkanTypes.h"
#include <memory>
#include <vector>

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

struct SwapChainImageAvailability {
    bool isAquired = false;
    uint32_t frameIndex = 0;
};

class SwapChain {
  public:
    SwapChain(VkDevice device, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, QueueFamilyIndices queueFamilyIndices,
              WindowContext *windowContext);
    ~SwapChain();

    void recreate();
    void destroy();

    /**
     * @brief Fired when this swapchain needs recreation.
     */
    EventSignal<void()> onRecreationRequested;

    /**
     * @brief Fired after this swapchain has been recreated.
     */
    EventSignal<void()> onRecreated;

    uint32_t getId() const { return m_id; }
    WindowContext *getWindowContext() const { return m_windowContext; }

    VkExtent2D getExtent() const { return m_swapChainExtent; }
    VkFormat getImageFormat() const { return m_swapChainImageFormat; }
    std::vector<VkImageView> getImageViews() const { return m_swapChainImageViews; }
    std::vector<VkImage> getImages() const { return m_swapChainImages; }
    VkSwapchainKHR getSwapChainVk() const { return m_swapChain; }
    uint32_t getImageCount() const { return m_imageCount; }
    std::shared_ptr<Texture> getDepthTexture() const { return m_depthTexture; }
    VkFormat getDepthImageFormat() const { return VK_FORMAT_D32_SFLOAT; }

    VkSemaphore getImageAvailableSemaphore(uint32_t frameIndex) const;
    VkSemaphore getRenderFinishedSemaphore(uint32_t imageIndex) const;
    VkFence getInFlightFence(uint32_t frameIndex) const;

    int acquireImage(uint32_t semaphoreIndex);
    void signalImageAvailability(uint32_t frameIndex);

    void presentImage();

    void invalidate();

    static RenderMode renderMode;

  private:
    void createImageViews();
    void createDepthTexture();
    void createSyncObjects();
    void destroySyncObjects();

    SwapChainSupportDetails2 querySwapChainSupport();
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities);

  private:
    static uint32_t s_nextID;

    uint32_t m_id;

    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapChainImages;
    std::vector<VkImageView> m_swapChainImageViews;
    VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapChainExtent{};

    uint32_t m_imageCount = 0;
    bool m_framebufferNeedsResize;

    std::shared_ptr<Texture> m_depthTexture;

    // only used for offscreen rendering when RenderMode is OFFSCREEN
    // std::vector<std::shared_ptr<Texture>> m_offscreenTextures;

    VkDevice m_device = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    QueueFamilyIndices m_queueFamilyIndices{};

    WindowContext *m_windowContext = nullptr;

    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;

    std::vector<SwapChainImageAvailability> m_semaphoreIndexToFrameIndexMap;
};

} // namespace Rapture

#include <vulkan/vulkan.h>
#include "WindowContext/WindowContext.h"

#include <optional>
#include <vector>


namespace Rapture {

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> computeFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() {
            return graphicsFamily.has_value() && computeFamily.has_value() && presentFamily.has_value();
        }
    };

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    class VulkanContext {
    public:
        VulkanContext(WindowContext* windowContext);
        ~VulkanContext();

    private:
        void createInstance(WindowContext* windowContext);
        void checkExtensionSupport();
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);

        std::vector<const char*> getRequiredExtensions(WindowContext* windowContext);

        // Validation layers
        bool checkValidationLayerSupport();
        void setupDebugMessenger();
    
        // setting up physical device
        void pickPhysicalDevice();
        bool isDeviceSuitable(VkPhysicalDevice device);

        // queue families
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);


        // logical device
        void createLogicalDevice();


        // surface
        void createWindowsSurface(WindowContext* windowContext);
        
        // swapchain
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, WindowContext* windowContext);

        void createSwapChain(WindowContext* windowContext);

        void createImageViews();

        void createGraphicsPipeline();

        VkShaderModule createShaderModule(const std::vector<char>& code);

    private:

        VkApplicationInfo m_applicationInfo;
        VkInstanceCreateInfo m_instanceCreateInfo;
        VkInstance m_instance;
        VkPhysicalDevice m_physicalDevice;
        VkDevice m_device;

        VkQueue m_graphicsQueue;
        VkQueue m_computeQueue;
        VkQueue m_transferQueue;
        VkQueue m_presentQueue;

        VkSurfaceKHR m_surface;

        VkDebugUtilsMessengerEXT m_debugMessenger;

        VkSwapchainKHR m_swapChain;


        std::vector<const char*> m_validationLayers;
        std::vector<const char*> m_deviceExtensions;

        std::vector<VkImage> m_swapChainImages;
        std::vector<VkImageView> m_swapChainImageViews;

        VkFormat m_swapChainImageFormat;
        VkExtent2D m_swapChainExtent;

    };

}
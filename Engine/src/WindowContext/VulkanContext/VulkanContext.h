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

    class VulkanContext {
    public:
        VulkanContext(WindowContext* windowContext);
        ~VulkanContext();

    private:
        void createInstance(WindowContext* windowContext);
        void checkExtensionSupport();


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

        std::vector<const char*> m_validationLayers;

    };

}
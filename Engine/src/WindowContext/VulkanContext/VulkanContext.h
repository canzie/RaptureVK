#pragma once


#include <vma/vk_mem_alloc.h>

#include "WindowContext/WindowContext.h"

#include "Buffers/Buffers.h"

#include "RenderTargets/FrameBuffers/FrameBuffer.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "RenderTargets/FrameBuffers/Renderpass.h"
#include "Pipelines/GraphicsPipeline.h"
#include "Shaders/Shader.h"

#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"

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


        //void drawFrame(WindowContext* windowContext);
        void waitIdle();

        VkDevice getLogicalDevice() const { return *m_device; }
        VkSurfaceKHR getSurface() const { return m_surface; }
        VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
        QueueFamilyIndices getQueueFamilyIndices() const;

        VmaAllocator getVmaAllocator() const { return m_vmaAllocator; }

        VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
        VkQueue getComputeQueue() const { return m_computeQueue; }
        VkQueue getTransferQueue() const { return m_transferQueue; }
        VkQueue getPresentQueue() const { return m_presentQueue; }


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
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;


        // logical device
        void createLogicalDevice();


        // surface
        void createWindowsSurface(WindowContext* windowContext);
        
        // swapchain
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, WindowContext* windowContext);


        void createVmaAllocator();




    private:


        VkApplicationInfo m_applicationInfo;
        VkInstanceCreateInfo m_instanceCreateInfo;
        VkInstance m_instance;
        VkPhysicalDevice m_physicalDevice;
        std::shared_ptr<VkDevice> m_device;

        VkQueue m_graphicsQueue;
        VkQueue m_computeQueue;
        VkQueue m_transferQueue;
        VkQueue m_presentQueue;

        VkSurfaceKHR m_surface;

        VkDebugUtilsMessengerEXT m_debugMessenger;




        std::vector<const char*> m_validationLayers;
        std::vector<const char*> m_deviceExtensions;



        VmaAllocator m_vmaAllocator;

    };

}
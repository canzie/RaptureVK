#pragma once

#include <vulkan/vulkan.h>
#include "WindowContext/WindowContext.h"

#include "Buffers/Buffers.h"

#include "RenderTargets/FrameBuffers/FrameBuffer.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "RenderTargets/FrameBuffers/Renderpass.h"
#include "Pipelines/GraphicsPipeline.h"
#include "Shaders/Shader.h"

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

        void createResources();


        void drawFrame(WindowContext* windowContext);
        void waitIdle();

        VkDevice getLogicalDevice() const { return *m_device; }
        VkSurfaceKHR getSurface() const { return m_surface; }
        VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
        QueueFamilyIndices getQueueFamilyIndices() const;


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

        void createSwapChain();

        void createGraphicsPipeline();


        void createRenderPass();
    
        void createFramebuffers();

        void createCommandPool();
        void createCommandBuffer();

        void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void createSyncObjects();

        void recreateSwapChain(WindowContext* windowContext);

        void cleanupSwapChain();

        void createDefaultShader();

        void createVertexBuffer();
        void createIndexBuffer();

        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    private:

        bool m_framebufferResized = false;

        uint32_t m_currentFrame = 0;

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

        SwapChain m_swapChain;

        VkPipelineLayout m_pipelineLayout;
        std::shared_ptr<Renderpass> m_renderPass;
        std::shared_ptr<GraphicsPipeline> m_graphicsPipeline;

        std::shared_ptr<Shader> m_shader;

        VkCommandPool m_commandPool;

        std::vector<VkCommandBuffer> m_commandBuffers;

        std::vector<const char*> m_validationLayers;
        std::vector<const char*> m_deviceExtensions;


        std::vector<VkDynamicState> m_dynamicStates;

        std::vector<FrameBuffer> m_swapChainFramebuffers;


        std::vector<VkSemaphore> m_imageAvailableSemaphores;
        std::vector<VkSemaphore> m_renderFinishedSemaphores;
        std::vector<VkFence> m_inFlightFences;


        std::shared_ptr<Buffer> m_indexBuffer;
        std::shared_ptr<Buffer> m_vertexBuffer;

    };

}
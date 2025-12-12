#pragma once

#include <vk_mem_alloc.h>

#include "WindowContext/VulkanContext/VulkanQueue.h"
#include "WindowContext/VulkanContext/VulkanTypes.h"
#include "WindowContext/WindowContext.h"

#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace Rapture {

class SwapChain;
class Renderpass;

class VulkanContext {
  public:
    VulkanContext(WindowContext *windowContext);
    ~VulkanContext();

    void waitIdle();

    VkDevice getLogicalDevice() const { return m_device; }
    VkSurfaceKHR getSurface() const { return m_surface; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkInstance getInstance() const { return m_instance; }
    QueueFamilyIndices getQueueFamilyIndices() const { return m_queueFamilyIndices; }
    std::shared_ptr<SwapChain> getSwapChain() const { return m_swapChain; }

    VmaAllocator getVmaAllocator() const { return m_vmaAllocator; }

    std::shared_ptr<VulkanQueue> getGraphicsQueue() const;
    std::shared_ptr<VulkanQueue> getComputeQueue() const;
    std::shared_ptr<VulkanQueue> getTransferQueue() const;
    std::shared_ptr<VulkanQueue> getPresentQueue() const;

    bool isVertexInputDynamicStateEnabled() const { return m_isVertexInputDynamicStateEnabled; }
    bool isVertexAttributeRobustnessEnabled() const { return m_isVertexAttributeRobustnessEnabled; }
    bool isDynamicRenderingEnabled() const { return m_isDynamicRenderingEnabled; }
    bool isNullDescriptorEnabled() const { return m_isNullDescriptorEnabled; }
    bool isRayTracingEnabled() const { return m_isRayTracingEnabled; }

    // Get descriptor indexing features
    const VkPhysicalDeviceDescriptorIndexingFeatures &getDescriptorIndexingFeatures() const { return m_descriptorIndexingFeatures; }

    // Get ray tracing properties
    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR &getRayTracingPipelineProperties() const
    {
        return m_rayTracingPipelineProperties;
    }

    const VkPhysicalDeviceAccelerationStructurePropertiesKHR &getAccelerationStructureProperties() const
    {
        return m_accelerationStructureProperties;
    }

    // Extension function pointers
    PFN_vkCmdSetVertexInputEXT vkCmdSetVertexInputEXT = nullptr;
    PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR = nullptr;
    PFN_vkCmdEndRenderingKHR vkCmdEndRenderingKHR = nullptr;
    PFN_vkCmdDrawMultiEXT vkCmdDrawMultiEXT = nullptr;
    PFN_vkCmdDrawMultiIndexedEXT vkCmdDrawMultiIndexedEXT = nullptr;

    // Ray tracing extension function pointers
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = nullptr;

    //
    void createRecourses(WindowContext *windowContext);

  private:
    void createInstance(WindowContext *windowContext);
    void checkExtensionSupport();
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);

    std::vector<const char *> getRequiredExtensions(WindowContext *windowContext);

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
    void createWindowsSurface(WindowContext *windowContext);

    // swapchain
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

    void createVmaAllocator();

  private:
    VkApplicationInfo m_applicationInfo;
    VkInstanceCreateInfo m_instanceCreateInfo;
    VkInstance m_instance;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;

    std::shared_ptr<SwapChain> m_swapChain;
    std::shared_ptr<Renderpass> m_renderpass;

    int m_graphicsQueueIndex;
    int m_computeQueueIndex;
    int m_transferQueueIndex;
    int m_presentQueueIndex;

    std::map<uint32_t, std::shared_ptr<VulkanQueue>> m_queues;
    std::shared_ptr<VulkanQueue> m_transferQueue;

    VkSurfaceKHR m_surface;

    VkDebugUtilsMessengerEXT m_debugMessenger;

    std::vector<const char *> m_validationLayers;
    std::vector<const char *> m_deviceExtensions;

    QueueFamilyIndices m_queueFamilyIndices;

    VmaAllocator m_vmaAllocator;

    bool m_isVertexInputDynamicStateEnabled;
    bool m_isVertexAttributeRobustnessEnabled;
    bool m_isDynamicRenderingEnabled;
    bool m_isNullDescriptorEnabled;
    bool m_isRayTracingEnabled;

    // Store descriptor indexing features support
    VkPhysicalDeviceDescriptorIndexingFeatures m_descriptorIndexingFeatures{};

    // Store ray tracing properties
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rayTracingPipelineProperties{};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR m_accelerationStructureProperties{};
};

} // namespace Rapture
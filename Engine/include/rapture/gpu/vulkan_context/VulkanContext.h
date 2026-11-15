#ifndef RAPTURE__VULKAN_CONTEXT_H
#define RAPTURE__VULKAN_CONTEXT_H

#include <vk_mem_alloc.h>

#include <rapture/gpu/vulkan_context/RenderContext.h>
#include <rapture/gpu/vulkan_context/VulkanQueue.h>
#include <rapture/gpu/vulkan_context/VulkanTypes.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace Rapture {

class SwapChain;
class Renderpass;

class VulkanContext {
  public:
    /**
     * @brief Creates the Vulkan instance
     * @param surfaceExtensions The instance extensions a window surface needs, empty for a context that never presents
     */
    explicit VulkanContext(const std::vector<const char *> &surfaceExtensions);
    ~VulkanContext();

    /**
     * @brief Picks the physical device and creates the logical device on it
     * @param surface The surface the device presents to, VK_NULL_HANDLE for a context that never presents
     * @param queueFamilyAssignment How the queue roles are spread over the device's queue families
     */
    void initDevice(VkSurfaceKHR surface, QueueFamilyAssignment queueFamilyAssignment);

    void waitIdle();

    /**
     * @brief Lets a buffer be used from every queue family the queue roles run on
     * @param createInfo The buffer's create info, its sharing mode and queue families are filled in
     */
    void shareAcrossQueueFamilies(VkBufferCreateInfo &createInfo) const;

    /**
     * @brief Lets an image be used from every queue family the queue roles run on
     * @param createInfo The image's create info, its sharing mode and queue families are filled in
     */
    void shareAcrossQueueFamilies(VkImageCreateInfo &createInfo) const;

    VkDevice getLogicalDevice() const { return m_device; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkInstance getInstance() const { return m_instance; }
    QueueFamilyIndices getQueueFamilyIndices() const { return m_queueFamilyIndices; }

    VmaAllocator getVmaAllocator() const { return m_vmaAllocator; }
    uint32_t getApiVersion() const { return m_applicationInfo.apiVersion; }

    /**
     * @brief Sums usage and budget across the device-local memory heaps
     * @param usedBytes Filled with current device-local usage
     * @param budgetBytes Filled with the driver-reported device-local budget
     */
    void getDeviceLocalMemoryUsage(uint64_t &usedBytes, uint64_t &budgetBytes) const;

    uint32_t getGraphicsQueueIndex() const { return m_queueFamilyIndices.familyIndices[GRAPHICS]; }
    uint32_t getComputeQueueIndex() const { return m_queueFamilyIndices.familyIndices[COMPUTE]; }
    uint32_t getTransferQueueIndex() const { return m_queueFamilyIndices.familyIndices[TRANSFER]; }
    uint32_t getPresentQueueIndex() const { return m_queueFamilyIndices.familyIndices[PRESENT]; }

    std::shared_ptr<VulkanQueue> getGraphicsQueue() const;
    std::shared_ptr<VulkanQueue> getComputeQueue() const;
    std::shared_ptr<VulkanQueue> getTransferQueue() const;
    std::shared_ptr<VulkanQueue> getPresentQueue() const;
    std::shared_ptr<VulkanQueue> getVendorQueue() const;

    bool isVertexInputDynamicStateEnabled() const { return m_isVertexInputDynamicStateEnabled; }
    bool isVertexAttributeRobustnessEnabled() const { return m_isVertexAttributeRobustnessEnabled; }
    bool isDynamicRenderingEnabled() const { return m_isDynamicRenderingEnabled; }
    bool isSynchronization2Enabled() const { return m_isSynchronization2Enabled; }
    bool isExtendedDynamicState3Enabled() const { return m_isExtendedDynamicState3Enabled; }
    bool isNullDescriptorEnabled() const { return m_isNullDescriptorEnabled; }
    bool isRayTracingEnabled() const { return m_isRayTracingEnabled; }
    bool isMeshShaderEnabled() const { return m_isMeshShaderEnabled; }

    const VkPhysicalDeviceAccelerationStructurePropertiesKHR &getAccelerationStructureProperties() const
    {
        return m_accelerationStructureProperties;
    }

    /**
     * @brief Colour attachments a single pass may render into on this device
     * @return The lower of maxColorAttachments and maxFragmentOutputAttachments
     */
    uint32_t getMaxColorAttachments() const { return m_maxColorAttachments; }

    /**
     * @brief Errors the validation layer has reported since the instance was created
     * @return The error count, always zero when validation is off
     */
    uint32_t getValidationErrorCount() const { return m_validationErrorCount.load(std::memory_order_relaxed); }

    // Extension function pointers
    PFN_vkCmdSetVertexInputEXT vkCmdSetVertexInputEXT = nullptr;
    PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR = nullptr;
    PFN_vkCmdEndRenderingKHR vkCmdEndRenderingKHR = nullptr;
    PFN_vkCmdDrawMultiEXT vkCmdDrawMultiEXT = nullptr;
    PFN_vkCmdDrawMultiIndexedEXT vkCmdDrawMultiIndexedEXT = nullptr;

    // Extended dynamic state 3 function pointers
    PFN_vkCmdSetPolygonModeEXT vkCmdSetPolygonModeEXT = nullptr;

    // Ray tracing extension function pointers
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkCmdCopyAccelerationStructureKHR vkCmdCopyAccelerationStructureKHR = nullptr;
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR vkCmdWriteAccelerationStructuresPropertiesKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = nullptr;

    // Device fault extension function pointers
    PFN_vkGetDeviceFaultInfoEXT vkGetDeviceFaultInfoEXT = nullptr;

    // Query and log device fault info after VK_ERROR_DEVICE_LOST
    void logDeviceFaultInfo();

    // Mesh shader extension function pointers
    PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT = nullptr;
    PFN_vkCmdDrawMeshTasksIndirectEXT vkCmdDrawMeshTasksIndirectEXT = nullptr;
    PFN_vkCmdDrawMeshTasksIndirectCountEXT vkCmdDrawMeshTasksIndirectCountEXT = nullptr;

    void initManagers();

    const RenderContext &getRenderContext() const { return m_renderContext; }

  private:
    void createInstance(const std::vector<const char *> &surfaceExtensions);
    void checkExtensionSupport();
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);

    std::vector<const char *> getRequiredExtensions(const std::vector<const char *> &surfaceExtensions);

    // Validation layers
    bool checkValidationLayerSupport();
    void setupDebugMessenger();

    // setting up physical device
    void pickPhysicalDevice(VkSurfaceKHR surface);
    bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);

    // queue families
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const;

    // logical device
    void createLogicalDevice(VkSurfaceKHR surface);

    // swapchain
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

    void createVmaAllocator();

  private:
    VkApplicationInfo m_applicationInfo;
    VkInstanceCreateInfo m_instanceCreateInfo;
    VkInstance m_instance;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;

    std::map<uint32_t, std::shared_ptr<VulkanQueue>> m_queues;
    std::shared_ptr<VulkanQueue> m_vendorQueue;

    VkDebugUtilsMessengerEXT m_debugMessenger;
    std::atomic<uint32_t> m_validationErrorCount{0};

    std::vector<const char *> m_validationLayers;
    std::vector<const char *> m_deviceExtensions;

    QueueFamilyIndices m_queueFamilyIndices;
    QueueFamilyAssignment m_queueFamilyAssignment = QueueFamilyAssignment::ONE_FAMILY;
    std::vector<uint32_t> m_sharedQueueFamilies;

    VmaAllocator m_vmaAllocator;

    bool m_isVertexInputDynamicStateEnabled;
    bool m_isVertexAttributeRobustnessEnabled;
    bool m_isDynamicRenderingEnabled;
    bool m_isSynchronization2Enabled;
    bool m_isExtendedDynamicState3Enabled;
    bool m_isNullDescriptorEnabled;
    bool m_isRayTracingEnabled;
    bool m_isMeshShaderEnabled;

    uint32_t m_maxColorAttachments = 4;

    // Store descriptor indexing features support
    VkPhysicalDeviceDescriptorIndexingFeatures m_descriptorIndexingFeatures{};

    // Store ray tracing properties
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rayTracingPipelineProperties{};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR m_accelerationStructureProperties{};

    std::unique_ptr<BufferPoolManager> m_bufferPoolManager;
    std::unique_ptr<CommandPoolManager> m_commandPoolManager;
    std::unique_ptr<DescriptorManager> m_descriptorManager;
    std::unique_ptr<AccelerationStructureBuilder> m_accelerationStructureBuilder;
    RenderContext m_renderContext;
};

} // namespace Rapture

#endif // RAPTURE__VULKAN_CONTEXT_H

#include "VulkanContext.h"
#include "Logging/Log.h"
#include "VulkanContextHelpers.h"

#include "RenderTargets/SwapChains/SwapChain.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <set>
#include <cstdint> 
#include <limits>
#include <algorithm> 

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif



namespace Rapture {


    VulkanContext::VulkanContext(WindowContext* windowContext)
    {

    m_applicationInfo = {};
    m_instanceCreateInfo = {};
    m_instance = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;
    m_surface = VK_NULL_HANDLE;
    m_vmaAllocator = VK_NULL_HANDLE;
    m_debugMessenger = VK_NULL_HANDLE;
    m_swapChain = nullptr;

    if (enableValidationLayers) {
        RP_CORE_INFO("Validation layers enabled!");
        m_validationLayers.push_back("VK_LAYER_KHRONOS_validation");
    } else {
        RP_CORE_INFO("Validation layers disabled!");
    }

    m_deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    m_deviceExtensions.push_back(VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);
    m_deviceExtensions.push_back(VK_EXT_VERTEX_ATTRIBUTE_ROBUSTNESS_EXTENSION_NAME);
    m_deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    m_deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    m_deviceExtensions.push_back(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
    
    // Ray tracing extensions
    m_deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    m_deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    m_deviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    m_deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);


    //checkExtensionSupport();
    createInstance(windowContext);
    setupDebugMessenger();
    createWindowsSurface(windowContext);

    pickPhysicalDevice();
    createLogicalDevice();
    createVmaAllocator();

    ApplicationEvents::onRequestSwapChainRecreation().addListener([this, windowContext]() {

        int width = 0, height = 0;
        windowContext->getFramebufferSize(&width, &height);
        while (width == 0 || height == 0) {
            windowContext->getFramebufferSize(&width, &height);
            windowContext->waitEvents();
        }

        vkDeviceWaitIdle(m_device);

        m_swapChain->recreate();
        ApplicationEvents::onSwapChainRecreated().publish(m_swapChain);
    });


    m_queueFamilyIndices = findQueueFamilies(m_physicalDevice);


}


VulkanContext::~VulkanContext()
{

    m_swapChain.reset();

    if (m_vmaAllocator) {
        vmaDestroyAllocator(m_vmaAllocator);
    }

    m_queues.clear();

    if (m_device) {
        vkDestroyDevice(m_device, nullptr);
    }

    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    }

    if (m_surface) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
    }

    RP_CORE_INFO("Destroyed Vulkan Context!");
}


void VulkanContext::waitIdle()
{
    vkDeviceWaitIdle(m_device);
}


std::shared_ptr<VulkanQueue> VulkanContext::getGraphicsQueue() const
{
    if (m_graphicsQueueIndex == -1) {
        RP_CORE_ERROR("Graphics queue index is -1!");
        throw std::runtime_error("Graphics queue index is -1!");
    }
    if (m_queues.find(m_graphicsQueueIndex) == m_queues.end()) {
        RP_CORE_ERROR("Graphics queue index is not found!");
        throw std::runtime_error("Graphics queue index is not found!");
    }
    return m_queues.find(m_graphicsQueueIndex)->second;
}

std::shared_ptr<VulkanQueue> VulkanContext::getComputeQueue() const
{
    if (m_computeQueueIndex == -1) {
        RP_CORE_ERROR("Compute queue index is -1!");
        throw std::runtime_error("Compute queue index is -1!");
    }
    if (m_queues.find(m_computeQueueIndex) == m_queues.end()) {
        RP_CORE_ERROR("Compute queue index is not found!");
        throw std::runtime_error("Compute queue index is not found!");
    }
    return m_queues.find(m_computeQueueIndex)->second;
}

std::shared_ptr<VulkanQueue> VulkanContext::getTransferQueue() const
{
    if (m_transferQueueIndex == -1) {
        RP_CORE_ERROR("Transfer queue index is -1!");
        throw std::runtime_error("Transfer queue index is -1!");
    }
    if (m_queues.find(m_transferQueueIndex) == m_queues.end()) {
        RP_CORE_ERROR("Transfer queue index is not found!");
        throw std::runtime_error("Transfer queue index is not found!");
    }
    return m_queues.find(m_transferQueueIndex)->second;
}

std::shared_ptr<VulkanQueue> VulkanContext::getPresentQueue() const
{
    if (m_presentQueueIndex == -1) {
        RP_CORE_ERROR("Present queue index is -1!");
        throw std::runtime_error("Present queue index is -1!");
    }
    if (m_queues.find(m_presentQueueIndex) == m_queues.end()) {
        RP_CORE_ERROR("Present queue index is not found!");
        throw std::runtime_error("Present queue index is not found!");
    }
    return m_queues.find(m_presentQueueIndex)->second;
}

void VulkanContext::createRecourses(WindowContext* windowContext)
{
    m_swapChain = std::make_shared<SwapChain>(m_device, m_surface, m_physicalDevice, m_queueFamilyIndices, windowContext);
    m_swapChain->invalidate();
}

void VulkanContext::createInstance(WindowContext *windowContext)
{

    if (enableValidationLayers && !checkValidationLayerSupport()) {
        RP_CORE_ERROR("Validation layers requested, but not available!");
        throw std::runtime_error("Validation layers requested, but not available!");
    }

    // Application info
    m_applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    m_applicationInfo.pApplicationName = "Rapture";
    m_applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    m_applicationInfo.pEngineName = "Rapture Engine";
    m_applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    m_applicationInfo.apiVersion = VK_API_VERSION_1_3;

    RP_CORE_INFO("Creating Vulkan instance with API version: {}.{}.{}", 
        VK_VERSION_MAJOR(m_applicationInfo.apiVersion),
        VK_VERSION_MINOR(m_applicationInfo.apiVersion),
        VK_VERSION_PATCH(m_applicationInfo.apiVersion));

    // Instance create info
    m_instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    m_instanceCreateInfo.pApplicationInfo = &m_applicationInfo;

    auto extensions = getRequiredExtensions(windowContext);
    m_instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    m_instanceCreateInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        m_instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        m_instanceCreateInfo.ppEnabledLayerNames = m_validationLayers.data();
            
        populateDebugMessengerCreateInfo(debugCreateInfo);
        m_instanceCreateInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
    } else {
        m_instanceCreateInfo.enabledLayerCount = 0;
        m_instanceCreateInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&m_instanceCreateInfo, nullptr, &m_instance) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create Vulkan instance!");
        throw std::runtime_error("Failed to create Vulkan instance!");
    }

    RP_CORE_INFO("Vulkan instance created successfully!");

}



void VulkanContext::checkExtensionSupport()
{
    RP_CORE_INFO("========== Supported Vulkan extensions: ==========");


    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> extensions(extensionCount);

    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

    for (const auto& extension : extensions) {
        RP_CORE_INFO("\t Extension: {0}", extension.extensionName);
    }

    RP_CORE_INFO("========================================================\n");
}

bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}


std::vector<const char *> VulkanContext::getRequiredExtensions(WindowContext* windowContext)
{
    uint32_t WCextensionCount = windowContext->getExtensionCount();
    const char** WCextensions = windowContext->getExtensions();

    std::vector<const char*> extensions(WCextensions, WCextensions + WCextensionCount);

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

bool VulkanContext::checkValidationLayerSupport() {

    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : m_validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

void VulkanContext::setupDebugMessenger()
{
    if (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    if (CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to set up debug messenger!");
        throw std::runtime_error("failed to set up debug messenger!");
    }

}



void VulkanContext::pickPhysicalDevice()
{
    m_physicalDevice = VK_NULL_HANDLE;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        RP_CORE_ERROR("Failed to find GPUs with Vulkan support!");
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            m_physicalDevice = device;
            break;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        RP_CORE_ERROR("failed to find a suitable GPU!");
        throw std::runtime_error("failed to find a suitable GPU!");
    }


}

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device) {
    
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    RP_CORE_INFO("GPU: {0}", deviceProperties.deviceName);

    
    QueueFamilyIndices indices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (indices.isComplete()) {
            break;
        }

        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        
        // Prefer a compute queue that also supports graphics for easier synchronization
        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            if (!indices.computeFamily.has_value() || 
                (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                indices.computeFamily = i;
            }
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);

        if (presentSupport) {
            indices.presentFamily = i;
        }

        i++;
    }

    return indices;
}

void VulkanContext::createLogicalDevice()
{

    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    
    // Add compute queue family if it's different from graphics/present
    if (indices.computeFamily.has_value()) {
        uniqueQueueFamilies.insert(indices.computeFamily.value());
    }

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Main structure to enable features; this will be pointed to by VkDeviceCreateInfo.pNext
    VkPhysicalDeviceFeatures2 physicalDeviceFeaturesToEnable{};
    physicalDeviceFeaturesToEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    // --- Handle Vulkan 1.0 Core Features ---
    // Query all supported Vulkan 1.0 features into the .features member.
    // vkGetPhysicalDeviceFeatures(m_physicalDevice, &physicalDeviceFeaturesToEnable.features); // Alternative
    // Or, more consistently with VkPhysicalDeviceFeatures2:
    VkPhysicalDeviceFeatures2 coreFeaturesQuery = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &coreFeaturesQuery);
    physicalDeviceFeaturesToEnable.features = coreFeaturesQuery.features; // Copy initially queried core features.

    // Ensure geometryShader is enabled if supported (as an example of a core feature)
    if (physicalDeviceFeaturesToEnable.features.geometryShader) {
        RP_CORE_INFO("Core::geometryShader is supported and enabled.");
    } else {
        RP_CORE_WARN("Core::geometryShader is NOT supported. If required, this could be an issue.");
        // If it's critical and not supported, you might throw an error or adapt.
        // For now, we'll try to enable it, and Vulkan will ignore if not supported (though we already know its status).
    }
    // physicalDeviceFeaturesToEnable.features.geometryShader remains as queried (true if supported, false otherwise)
    // If you wanted to force it ON and it was a settable feature you'd do it here, but geometryShader is typically just reported.
    // If you want to *request* it, and it's supported, it will be enabled. If not supported, request is ignored.
    // The most robust is to check support and then request. If geometryShader is critical, one might check
    // coreFeaturesQuery.features.geometryShader and throw if not present.

    // Initialize pNext pointer for chaining extension features
    void** ppNextChain = &physicalDeviceFeaturesToEnable.pNext;

    // --- VK_EXT_descriptor_indexing features ---
    // Initialize and query descriptor indexing features
    m_descriptorIndexingFeatures = {};
    m_descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    
    VkPhysicalDeviceFeatures2 queryDescriptorIndexing = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    queryDescriptorIndexing.pNext = &m_descriptorIndexingFeatures;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &queryDescriptorIndexing);
    
    // Enable required descriptor indexing features
    m_descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    m_descriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
    m_descriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
    m_descriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
    
    // Chain the descriptor indexing features
    *ppNextChain = &m_descriptorIndexingFeatures;
    ppNextChain = &m_descriptorIndexingFeatures.pNext;

    // --- VK_EXT_vertex_input_dynamic_state ---
    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT dynamicStateFeaturesToEnable{};
    dynamicStateFeaturesToEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT;
    
    // Query specifically for this extension's features
    VkPhysicalDeviceFeatures2 queryDynamicState = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    queryDynamicState.pNext = &dynamicStateFeaturesToEnable; // Temporarily chain for querying
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &queryDynamicState);
    // After the call, dynamicStateFeaturesToEnable.vertexInputDynamicState holds the support status

    if (dynamicStateFeaturesToEnable.vertexInputDynamicState) {
        RP_CORE_INFO("Feature EXT::vertexInputDynamicState is supported and will be enabled.");
        // The struct dynamicStateFeaturesToEnable is already populated with sType and the feature set to VK_TRUE.
        // We just need to chain it into the main physicalDeviceFeaturesToEnable.pNext chain.
        *ppNextChain = &dynamicStateFeaturesToEnable;
        ppNextChain = &dynamicStateFeaturesToEnable.pNext; // Advance the tail of our chain
    } else {
        RP_CORE_WARN("Feature EXT::vertexInputDynamicState is NOT supported.");
    }

    // --- VK_EXT_vertex_attribute_robustness ---
    VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT attributeRobustnessFeaturesToEnable{};
    attributeRobustnessFeaturesToEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_ROBUSTNESS_FEATURES_EXT;

    // Query specifically for this extension's features
    VkPhysicalDeviceFeatures2 queryAttributeRobustness = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    queryAttributeRobustness.pNext = &attributeRobustnessFeaturesToEnable; // Temporarily chain for querying
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &queryAttributeRobustness);
    // After the call, attributeRobustnessFeaturesToEnable.vertexAttributeRobustness holds the support status

    if (attributeRobustnessFeaturesToEnable.vertexAttributeRobustness) {
        RP_CORE_INFO("Feature EXT::vertexAttributeRobustness is supported and will be enabled.");
        // The struct attributeRobustnessFeaturesToEnable is already populated with sType and the feature set to VK_TRUE.
        // Chain it.
        *ppNextChain = &attributeRobustnessFeaturesToEnable;
        ppNextChain = &attributeRobustnessFeaturesToEnable.pNext; // Advance the tail of our chain
    } else {
        RP_CORE_WARN("Feature EXT::vertexAttributeRobustness is NOT supported.");
    }



    // --- VK_KHR_dynamic_rendering ---
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeaturesToEnable{};
    dynamicRenderingFeaturesToEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;

    // Query specifically for this extension's features
    VkPhysicalDeviceFeatures2 queryDynamicRendering = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    queryDynamicRendering.pNext = &dynamicRenderingFeaturesToEnable; // Temporarily chain for querying
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &queryDynamicRendering);

    if (dynamicRenderingFeaturesToEnable.dynamicRendering) {
        RP_CORE_INFO("Feature KHR::dynamicRendering is supported and will be enabled.");
        *ppNextChain = &dynamicRenderingFeaturesToEnable;
        ppNextChain = &dynamicRenderingFeaturesToEnable.pNext;
    } else {
        RP_CORE_WARN("Feature KHR::dynamicRendering is NOT supported.");
    }

    // --- VK_KHR_robustness2 ---
    VkPhysicalDeviceRobustness2FeaturesEXT robustness2FeaturesToEnable{};
    robustness2FeaturesToEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;

    // Query specifically for this extension's features
    VkPhysicalDeviceFeatures2 queryRobustness2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    queryRobustness2.pNext = &robustness2FeaturesToEnable; // Temporarily chain for querying
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &queryRobustness2);

    if (robustness2FeaturesToEnable.nullDescriptor) {
        RP_CORE_INFO("Feature KHR::robustness2::nullDescriptor is supported and will be enabled.");
        // Enable null descriptor feature to handle sparse descriptor sets
        robustness2FeaturesToEnable.nullDescriptor = VK_TRUE;
        *ppNextChain = &robustness2FeaturesToEnable;
        ppNextChain = &robustness2FeaturesToEnable.pNext;
    } else {
        RP_CORE_WARN("Feature KHR::robustness2::nullDescriptor is NOT supported.");
    }

    // --- Ray Tracing Features ---
    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR bufferDeviceAddressFeaturesToEnable{};
    bufferDeviceAddressFeaturesToEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
    
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeaturesToEnable{};
    accelerationStructureFeaturesToEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeaturesToEnable{};
    rayTracingPipelineFeaturesToEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;

    // Query buffer device address features
    VkPhysicalDeviceFeatures2 queryBufferDeviceAddress = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    queryBufferDeviceAddress.pNext = &bufferDeviceAddressFeaturesToEnable;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &queryBufferDeviceAddress);

    // Query acceleration structure features
    VkPhysicalDeviceFeatures2 queryAccelerationStructure = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    queryAccelerationStructure.pNext = &accelerationStructureFeaturesToEnable;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &queryAccelerationStructure);

    // Query ray tracing pipeline features
    VkPhysicalDeviceFeatures2 queryRayTracingPipeline = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    queryRayTracingPipeline.pNext = &rayTracingPipelineFeaturesToEnable;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &queryRayTracingPipeline);

    // Enable ray tracing features if supported
    bool rayTracingSupported = bufferDeviceAddressFeaturesToEnable.bufferDeviceAddress &&
                              accelerationStructureFeaturesToEnable.accelerationStructure &&
                              rayTracingPipelineFeaturesToEnable.rayTracingPipeline;

    if (rayTracingSupported) {
        RP_CORE_INFO("Ray tracing is supported and will be enabled.");
        
        // Enable buffer device address
        bufferDeviceAddressFeaturesToEnable.bufferDeviceAddress = VK_TRUE;
        *ppNextChain = &bufferDeviceAddressFeaturesToEnable;
        ppNextChain = &bufferDeviceAddressFeaturesToEnable.pNext;
        
        // Enable acceleration structure
        accelerationStructureFeaturesToEnable.accelerationStructure = VK_TRUE;
        *ppNextChain = &accelerationStructureFeaturesToEnable;
        ppNextChain = &accelerationStructureFeaturesToEnable.pNext;
        
        // Enable ray tracing pipeline
        rayTracingPipelineFeaturesToEnable.rayTracingPipeline = VK_TRUE;
        *ppNextChain = &rayTracingPipelineFeaturesToEnable;
        ppNextChain = &rayTracingPipelineFeaturesToEnable.pNext;
        
        m_isRayTracingEnabled = true;
    } else {
        RP_CORE_WARN("Ray tracing is NOT supported on this device.");
        m_isRayTracingEnabled = false;
    }
    
    // Ensure the end of the chain is nullptr if no more features are added
    *ppNextChain = nullptr;


    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    
    createInfo.pNext = &physicalDeviceFeaturesToEnable; // Point to the head of our features chain
    createInfo.pEnabledFeatures = nullptr; // Must be nullptr if pNext includes VkPhysicalDeviceFeatures2

    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }


    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to create logical device!");
        throw std::runtime_error("failed to create logical device!");
    }


    // Load extension function pointers
    if (dynamicStateFeaturesToEnable.vertexInputDynamicState) {
        vkCmdSetVertexInputEXT = (PFN_vkCmdSetVertexInputEXT)vkGetDeviceProcAddr(m_device, "vkCmdSetVertexInputEXT");
        if (!vkCmdSetVertexInputEXT) {
            RP_CORE_ERROR("Failed to load vkCmdSetVertexInputEXT function pointer!");
            m_isVertexInputDynamicStateEnabled = false;
        } else {
            m_isVertexInputDynamicStateEnabled = true;
            RP_CORE_INFO("Successfully loaded vkCmdSetVertexInputEXT function pointer.");
        }
    } else {
        m_isVertexInputDynamicStateEnabled = false;
    }

    // Store vertex attribute robustness state
    m_isVertexAttributeRobustnessEnabled = attributeRobustnessFeaturesToEnable.vertexAttributeRobustness;

    // Store robustness2 state
    m_isNullDescriptorEnabled = robustness2FeaturesToEnable.nullDescriptor;

    // Store dynamic rendering state and load function pointers
    if (dynamicRenderingFeaturesToEnable.dynamicRendering) {
        vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(m_device, "vkCmdBeginRenderingKHR");
        vkCmdEndRenderingKHR = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(m_device, "vkCmdEndRenderingKHR");
        if (!vkCmdBeginRenderingKHR || !vkCmdEndRenderingKHR) {
            RP_CORE_ERROR("Failed to load dynamic rendering function pointers!");
            m_isDynamicRenderingEnabled = false;
        } else {
            m_isDynamicRenderingEnabled = true;
            RP_CORE_INFO("Successfully loaded dynamic rendering function pointers.");
        }
    } else {
        m_isDynamicRenderingEnabled = false;
    }

    // Load ray tracing function pointers and query properties
    if (m_isRayTracingEnabled) {
        // Load acceleration structure function pointers
        vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(m_device, "vkCreateAccelerationStructureKHR");
        vkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(m_device, "vkDestroyAccelerationStructureKHR");
        vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureBuildSizesKHR");
        vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(m_device, "vkCmdBuildAccelerationStructuresKHR");
        vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureDeviceAddressKHR");

        // Load ray tracing pipeline function pointers
        vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(m_device, "vkCreateRayTracingPipelinesKHR");
        vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(m_device, "vkGetRayTracingShaderGroupHandlesKHR");
        vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(m_device, "vkCmdTraceRaysKHR");

        // Verify all function pointers were loaded successfully
        if (!vkCreateAccelerationStructureKHR || !vkDestroyAccelerationStructureKHR || 
            !vkGetAccelerationStructureBuildSizesKHR || !vkCmdBuildAccelerationStructuresKHR ||
            !vkGetAccelerationStructureDeviceAddressKHR || !vkCreateRayTracingPipelinesKHR ||
            !vkGetRayTracingShaderGroupHandlesKHR || !vkCmdTraceRaysKHR) {
            RP_CORE_ERROR("Failed to load some ray tracing function pointers!");
            m_isRayTracingEnabled = false;
        } else {
            RP_CORE_INFO("Successfully loaded all ray tracing function pointers.");
            
            // Query ray tracing properties
            m_rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
            m_accelerationStructureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
            
            VkPhysicalDeviceProperties2 deviceProperties2{};
            deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            deviceProperties2.pNext = &m_rayTracingPipelineProperties;
            m_rayTracingPipelineProperties.pNext = &m_accelerationStructureProperties;
            
            vkGetPhysicalDeviceProperties2(m_physicalDevice, &deviceProperties2);
            
            RP_CORE_INFO("Ray tracing properties queried successfully.");
            RP_CORE_INFO("  Max ray recursion depth: {}", m_rayTracingPipelineProperties.maxRayRecursionDepth);
            RP_CORE_INFO("  Shader group handle size: {}", m_rayTracingPipelineProperties.shaderGroupHandleSize);
        }
    }

    // retrieve queues
    if (indices.graphicsFamily.value() == indices.presentFamily.value()) {
        // If graphics and present queues are from the same family, use the same instance
        auto queue = std::make_shared<VulkanQueue>(m_device, indices.graphicsFamily.value());
        m_queues[indices.graphicsFamily.value()] = queue;
        m_graphicsQueueIndex = indices.graphicsFamily.value();
        m_presentQueueIndex = indices.graphicsFamily.value();
    } else {
        // If they're from different families, create separate instances
        m_queues[indices.graphicsFamily.value()] = std::make_shared<VulkanQueue>(m_device, indices.graphicsFamily.value());
        m_queues[indices.presentFamily.value()] = std::make_shared<VulkanQueue>(m_device, indices.presentFamily.value());
        m_graphicsQueueIndex = indices.graphicsFamily.value();
        m_presentQueueIndex = indices.presentFamily.value();
    }

    // Create compute queue
    if (indices.computeFamily.has_value()) {
        // Check if compute queue family is different from already created queues
        if (m_queues.find(indices.computeFamily.value()) == m_queues.end()) {
            m_queues[indices.computeFamily.value()] = std::make_shared<VulkanQueue>(m_device, indices.computeFamily.value());
        }
        m_computeQueueIndex = indices.computeFamily.value();
        RP_CORE_INFO("Compute queue created using family index: {}", indices.computeFamily.value());
    } else {
        m_computeQueueIndex = -1;
        RP_CORE_WARN("No compute queue family found!");
    }

    m_transferQueueIndex = -1;

    RP_CORE_INFO("Logical device created successfully!");
}


void VulkanContext::createWindowsSurface(WindowContext *windowContext)
{
    if (glfwCreateWindowSurface(m_instance, (GLFWwindow*)windowContext->getNativeWindowContext(), nullptr, &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
    RP_CORE_INFO("Window surface created successfully!");
}

SwapChainSupportDetails VulkanContext::querySwapChainSupport(VkPhysicalDevice device)
{
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}



void VulkanContext::createVmaAllocator()
{
    RP_CORE_INFO("Starting VMA allocator creation...");
    
    RP_CORE_INFO("Checking Vulkan handles...");
    if (!m_physicalDevice) {
        RP_CORE_ERROR("Physical device is null!");
        throw std::runtime_error("Physical device is null during VMA allocator creation");
    }
    if (!m_device) {
        RP_CORE_ERROR("Logical device is null!");
        throw std::runtime_error("Logical device is null during VMA allocator creation");
    }
    if (!m_instance) {
        RP_CORE_ERROR("Vulkan instance is null!");
        throw std::runtime_error("Vulkan instance is null during VMA allocator creation");
    }
    RP_CORE_INFO("All Vulkan handles valid");

    RP_CORE_INFO("Creating VMA allocator create info...");
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    
    RP_CORE_INFO("Calling vmaCreateAllocator...");
    VkResult result = vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create VMA allocator!");
        throw std::runtime_error("Failed to create VMA allocator!");
    }
    
    RP_CORE_INFO("Successfully created VMA allocator");
}









}

#include "VulkanContext.h"
#include "Logging/Log.h"
#include "VulkanContextHelpers.h"

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "RenderTargets/SwapChains/SwapChain.h"

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__linux__)
// Runtime detection for Wayland vs X11
#include <cstdlib>
static bool isWaylandSession()
{
    return std::getenv("WAYLAND_DISPLAY") != nullptr;
}

#define RAPTURE_RUNTIME_WAYLAND_DETECTION
// We'll define the appropriate platform at runtime, but include both headers
#define VK_USE_PLATFORM_X11_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#ifdef RAPTURE_RUNTIME_WAYLAND_DETECTION
// Include Vulkan surface extension headers
#include <vulkan/vulkan_wayland.h>
// Only include X11 headers if X11 development files are available
#ifdef __has_include
#if __has_include(<X11/Xlib.h>)
#include <X11/Xlib.h> // Must include X11 types before vulkan_xlib.h
#include <vulkan/vulkan_xlib.h>
#define RAPTURE_HAS_X11_HEADERS
#endif
#else
// Fallback for older compilers - try to include and let it fail gracefully
#ifdef VK_USE_PLATFORM_X11_KHR
#include <X11/Xlib.h> // Must include X11 types before vulkan_xlib.h
#include <vulkan/vulkan_xlib.h>
#define RAPTURE_HAS_X11_HEADERS
#endif
#endif
#endif

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
#ifdef RAPTURE_RUNTIME_WAYLAND_DETECTION
// Include both native headers since we detect at runtime
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#endif

#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

namespace Rapture {

VulkanContext::VulkanContext(WindowContext *windowContext)
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
    m_vendorQueue = nullptr;
    m_isVertexInputDynamicStateEnabled = false;
    m_isVertexAttributeRobustnessEnabled = false;
    m_isDynamicRenderingEnabled = false;
    m_isNullDescriptorEnabled = false;
    m_isRayTracingEnabled = false;

#ifdef RAPTURE_RUNTIME_WAYLAND_DETECTION
    // Detect and log the windowing system being used
    bool usingWayland = isWaylandSession();
    if (usingWayland) {
        RP_CORE_INFO("Detected Wayland session - using Wayland surface support");
    } else {
        RP_CORE_INFO("Detected X11 session - using X11 surface support");
    }
#endif

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
    m_deviceExtensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
    m_deviceExtensions.push_back(VK_EXT_MULTI_DRAW_EXTENSION_NAME);

    // Ray tracing extensions
    m_deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    m_deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    m_deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    m_deviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    m_deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

    checkExtensionSupport();
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
}

VulkanContext::~VulkanContext()
{
    if (m_device) {
        vkDeviceWaitIdle(m_device);
    }

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
    uint32_t graphicsQueueIndex = m_queueFamilyIndices.familyIndices[GRAPHICS];
    if (graphicsQueueIndex == UINT32_MAX) {
        RP_CORE_ERROR("Graphics queue index is UINT32_MAX!");
        throw std::runtime_error("Graphics queue index is UINT32_MAX!");
    }
    if (m_queues.find(graphicsQueueIndex) == m_queues.end()) {
        RP_CORE_ERROR("Graphics queue index is not found!");
        throw std::runtime_error("Graphics queue index is not found!");
    }
    return m_queues.find(graphicsQueueIndex)->second;
}

std::shared_ptr<VulkanQueue> VulkanContext::getComputeQueue() const
{
    uint32_t computeQueueIndex = m_queueFamilyIndices.familyIndices[COMPUTE];
    if (computeQueueIndex == UINT32_MAX) {
        RP_CORE_ERROR("Compute queue index is UINT32_MAX!");
        throw std::runtime_error("Compute queue index is UINT32_MAX!");
    }
    if (m_queues.find(computeQueueIndex) == m_queues.end()) {
        RP_CORE_ERROR("Compute queue index is not found!");
        throw std::runtime_error("Compute queue index is not found!");
    }
    return m_queues.find(computeQueueIndex)->second;
}

std::shared_ptr<VulkanQueue> VulkanContext::getTransferQueue() const
{
    uint32_t transferQueueIndex = m_queueFamilyIndices.familyIndices[TRANSFER];
    if (transferQueueIndex == UINT32_MAX) {
        RP_CORE_ERROR("Transfer queue index is UINT32_MAX!");
        throw std::runtime_error("Transfer queue index is UINT32_MAX!");
    }
    if (m_queues.find(transferQueueIndex) == m_queues.end()) {
        RP_CORE_ERROR("Transfer queue index is not found!");
        throw std::runtime_error("Transfer queue index is not found!");
    }
    return m_queues.find(transferQueueIndex)->second;
}

std::shared_ptr<VulkanQueue> VulkanContext::getPresentQueue() const
{
    uint32_t presentQueueIndex = m_queueFamilyIndices.familyIndices[PRESENT];
    if (presentQueueIndex == UINT32_MAX) {
        RP_CORE_ERROR("Present queue index is UINT32_MAX!");
        throw std::runtime_error("Present queue index is UINT32_MAX!");
    }
    if (m_queues.find(presentQueueIndex) == m_queues.end()) {
        RP_CORE_ERROR("Present queue index is not found!");
        throw std::runtime_error("Present queue index is not found!");
    }
    return m_queues.find(presentQueueIndex)->second;
}

std::shared_ptr<VulkanQueue> VulkanContext::getVendorQueue() const
{
    return m_vendorQueue != nullptr ? m_vendorQueue : getGraphicsQueue();
}

void VulkanContext::createRecourses(WindowContext *windowContext)
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

    RP_CORE_INFO("Creating Vulkan instance with API version: {}.{}.{}", VK_VERSION_MAJOR(m_applicationInfo.apiVersion),
                 VK_VERSION_MINOR(m_applicationInfo.apiVersion), VK_VERSION_PATCH(m_applicationInfo.apiVersion));

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
        m_instanceCreateInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
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

#ifdef RAPTURE_RUNTIME_WAYLAND_DETECTION
    bool hasWaylandSupport = false;
    bool hasX11Support = false;
#endif

    for (const auto &extension : extensions) {
        RP_CORE_INFO("\t Extension: {0}", extension.extensionName);

#ifdef RAPTURE_RUNTIME_WAYLAND_DETECTION
        if (strcmp(extension.extensionName, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME) == 0) {
            hasWaylandSupport = true;
        }
#ifdef RAPTURE_HAS_X11_HEADERS
        if (strcmp(extension.extensionName, VK_KHR_XLIB_SURFACE_EXTENSION_NAME) == 0) {
            hasX11Support = true;
        }
#endif
#endif
    }

#ifdef RAPTURE_RUNTIME_WAYLAND_DETECTION
    bool usingWayland = isWaylandSession();
    if (usingWayland) {
        if (hasWaylandSupport) {
            RP_CORE_INFO("Wayland surface extension is supported!");
        } else {
            RP_CORE_ERROR("Wayland surface extension is NOT supported!");
        }
#ifdef RAPTURE_HAS_X11_HEADERS
    } else {
        if (hasX11Support) {
            RP_CORE_INFO("X11 surface extension is supported!");
        } else {
            RP_CORE_ERROR("X11 surface extension is NOT supported!");
        }
#endif
    }
#endif

    RP_CORE_INFO("========================================================\n");
}

bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device)
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());

    for (const auto &extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

std::vector<const char *> VulkanContext::getRequiredExtensions(WindowContext *windowContext)
{
    uint32_t WCextensionCount = windowContext->getExtensionCount();
    const char **WCextensions = windowContext->getExtensions();

    std::vector<const char *> extensions(WCextensions, WCextensions + WCextensionCount);

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

#ifdef RAPTURE_RUNTIME_WAYLAND_DETECTION
    // Log the surface extension being requested
    bool hasWaylandSurface = false;
    bool hasX11Surface = false;
    for (size_t i = 0; i < extensions.size(); ++i) {
        if (strcmp(extensions[i], VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME) == 0) {
            hasWaylandSurface = true;
        }
#ifdef RAPTURE_HAS_X11_HEADERS
        if (strcmp(extensions[i], VK_KHR_XLIB_SURFACE_EXTENSION_NAME) == 0) {
            hasX11Surface = true;
        }
#endif
    }

    bool usingWayland = isWaylandSession();
    if (usingWayland && hasWaylandSurface) {
        RP_CORE_INFO("Requesting Wayland surface extension for Wayland session");
#ifdef RAPTURE_HAS_X11_HEADERS
    } else if (!usingWayland && hasX11Surface) {
        RP_CORE_INFO("Requesting X11 surface extension for X11 session");
#endif
    } else if (usingWayland && !hasWaylandSurface) {
        RP_CORE_WARN("Wayland session detected but no Wayland surface extension requested");
#ifdef RAPTURE_HAS_X11_HEADERS
    } else if (!usingWayland && !hasX11Surface) {
        RP_CORE_WARN("X11 session detected but no X11 surface extension requested");
#endif
    }
#endif

    return extensions;
}

bool VulkanContext::checkValidationLayerSupport()
{

    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char *layerName : m_validationLayers) {
        bool layerFound = false;

        for (const auto &layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            RP_CORE_ERROR("Required validation layer '{}' not found!", layerName);
            RP_CORE_ERROR("Available validation layers:");
            for (const auto &layerProperties : availableLayers) {
                RP_CORE_ERROR("  - {}", layerProperties.layerName);
            }
            if (availableLayers.empty()) {
                RP_CORE_ERROR("  (No validation layers available)");
            }
            RP_CORE_ERROR("To install validation layers on Arch Linux, run: sudo pacman -S vulkan-validation-layers");
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

    for (const auto &device : devices) {
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

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device)
{

    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    RP_CORE_INFO("Evaluating GPU: {0}", deviceProperties.deviceName);

    bool isDeviceSuitable = true;

    // Check queue family indices
    QueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.isComplete()) {
        RP_CORE_WARN("GPU {0}: Queue families incomplete:", deviceProperties.deviceName);
        if (indices.familyIndices[GRAPHICS] == UINT32_MAX) {
            RP_CORE_WARN("\t- Graphics queue family not found");
        }
        if (indices.familyIndices[PRESENT] == UINT32_MAX) {
            RP_CORE_WARN("\t- Present queue family not found");
        }
        if (indices.familyIndices[COMPUTE] == UINT32_MAX) {
            RP_CORE_WARN("\t- Compute queue family not found");
        }
        isDeviceSuitable = false;
    } else {
        RP_CORE_INFO("GPU {0}: All required queue families found", deviceProperties.deviceName);
    }

    // Check device extension support
    bool extensionsSupported = checkDeviceExtensionSupport(device);
    if (!extensionsSupported) {
        RP_CORE_WARN("GPU {0}: Required device extensions not supported", deviceProperties.deviceName);

        // Get available extensions for this device
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        // Create set of available extension names for quick lookup
        std::set<std::string> availableExtensionNames;
        for (const auto &extension : availableExtensions) {
            availableExtensionNames.insert(extension.extensionName);
        }

        // Log missing extensions
        RP_CORE_WARN("  Missing required extensions:");
        for (const auto &requiredExtension : m_deviceExtensions) {
            if (availableExtensionNames.find(requiredExtension) == availableExtensionNames.end()) {
                RP_CORE_WARN("    - {0}", requiredExtension);
            }
        }
        isDeviceSuitable = false;
    } else {
        RP_CORE_INFO("GPU {0}: All required device extensions supported", deviceProperties.deviceName);
    }

    // Check swap chain support
    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();

        if (!swapChainAdequate) {
            RP_CORE_WARN("GPU {0}: Swap chain support inadequate:", deviceProperties.deviceName);
            if (swapChainSupport.formats.empty()) {
                RP_CORE_WARN("  - No surface formats available");
            }
            if (swapChainSupport.presentModes.empty()) {
                RP_CORE_WARN("  - No present modes available");
            }
            isDeviceSuitable = false;
        } else {
            RP_CORE_INFO("GPU {0}: Swap chain support adequate ({1} formats, {2} present modes)", deviceProperties.deviceName,
                         swapChainSupport.formats.size(), swapChainSupport.presentModes.size());
        }
    } else {
        RP_CORE_WARN("GPU {0}: Cannot check swap chain support - extensions not supported", deviceProperties.deviceName);
        isDeviceSuitable = false;
    }

    // Final result logging
    if (isDeviceSuitable) {
        RP_CORE_INFO("GPU {0}: Device is SUITABLE for use", deviceProperties.deviceName);
    } else {
        RP_CORE_WARN("GPU {0}: Device is NOT SUITABLE for use", deviceProperties.deviceName);
    }

    return isDeviceSuitable;
}

#define PREFER_QUEUE_INDEX(type)                                               \
    (queueIndicesUses[indices.familyIndices[type]] < queueFamily.queueCount || \
     queueIndicesUses[i] < queueIndicesUses[indices.familyIndices[type]])

QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    std::vector<uint32_t> queueIndicesUses(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto &queueFamily : queueFamilies) {
        if (indices.isComplete()) {
            break;
        }

        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            if (indices.familyIndices[GRAPHICS] == UINT32_MAX) {
                indices.familyIndices[GRAPHICS] = i;
                indices.familyQueueCounts[GRAPHICS] = queueFamily.queueCount;
                queueIndicesUses[i]++;
            } else if (PREFER_QUEUE_INDEX(GRAPHICS)) {
                queueIndicesUses[indices.familyIndices[GRAPHICS]]--;
                indices.familyIndices[GRAPHICS] = i;
                indices.familyQueueCounts[GRAPHICS] = queueFamily.queueCount;
                queueIndicesUses[i]++;
            }
        }
        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            if (indices.familyIndices[COMPUTE] == UINT32_MAX) {
                indices.familyIndices[COMPUTE] = i;
                indices.familyQueueCounts[COMPUTE] = queueFamily.queueCount;
                queueIndicesUses[i]++;
            } else if (PREFER_QUEUE_INDEX(COMPUTE)) {
                queueIndicesUses[indices.familyIndices[COMPUTE]]--;
                indices.familyIndices[COMPUTE] = i;
                indices.familyQueueCounts[COMPUTE] = queueFamily.queueCount;
                queueIndicesUses[i]++;
            }
        }

        if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) {
            if (indices.familyIndices[TRANSFER] == UINT32_MAX) {
                indices.familyIndices[TRANSFER] = i;
                indices.familyQueueCounts[TRANSFER] = queueFamily.queueCount;
                queueIndicesUses[i]++;
            } else if (PREFER_QUEUE_INDEX(TRANSFER)) {
                queueIndicesUses[indices.familyIndices[TRANSFER]]--;
                indices.familyIndices[TRANSFER] = i;
                indices.familyQueueCounts[TRANSFER] = queueFamily.queueCount;
                queueIndicesUses[i]++;
            }
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);

        if (presentSupport) {
            if (indices.familyIndices[PRESENT] == UINT32_MAX) {
                indices.familyIndices[GRAPHICS] = i;
                indices.familyQueueCounts[GRAPHICS] = queueFamily.queueCount;
                queueIndicesUses[i]++;
            } else if (PREFER_QUEUE_INDEX(PRESENT)) {
                queueIndicesUses[indices.familyIndices[PRESENT]]--;

                indices.familyIndices[PRESENT] = i;
                indices.familyQueueCounts[PRESENT] = queueFamily.queueCount;
                queueIndicesUses[i]++;
            }
        }

        i++;
    }

    indices.print();

    return indices;
}

static void s_appendToPNextChain(VkPhysicalDeviceFeatures2 &root, VkBaseOutStructure *node)
{
    VkBaseOutStructure *current = reinterpret_cast<VkBaseOutStructure *>(&root);

    while (current->pNext) {
        current = reinterpret_cast<VkBaseOutStructure *>(current->pNext);
    }

    current->pNext = node;
}

static void s_enableCoreFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 &featuresToEnable)
{
    VkPhysicalDeviceFeatures2 supported{};
    supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &supported);

    if (supported.features.geometryShader) {
        featuresToEnable.features.geometryShader = VK_TRUE;
    } else {
        RP_CORE_WARN("geometryShader NOT supported.");
    }

    if (supported.features.tessellationShader) {
        featuresToEnable.features.tessellationShader = VK_TRUE;
    } else {
        RP_CORE_WARN("tessellationShader NOT supported.");
    }

    if (supported.features.multiDrawIndirect) {
        featuresToEnable.features.multiDrawIndirect = VK_TRUE;
    } else {
        RP_CORE_WARN("multiDrawIndirect NOT supported.");
    }

    if (supported.features.fillModeNonSolid) {
        featuresToEnable.features.fillModeNonSolid = VK_TRUE;
    } else {
        RP_CORE_WARN("fillModeNonSolid NOT supported.");
    }

    if (supported.features.samplerAnisotropy) {
        featuresToEnable.features.samplerAnisotropy = VK_TRUE;
    } else {
        RP_CORE_WARN("Sampler anisotropy not supported; disabling anisotropy in samplers");
    }
}

static void s_enableDescriptorIndexingFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 &featuresToEnable,
                                               VkPhysicalDeviceDescriptorIndexingFeatures &descriptorIndexingFeatures)
{
    VkPhysicalDeviceDescriptorIndexingFeatures supported{};
    supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

    VkPhysicalDeviceFeatures2 query{};
    query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    query.pNext = &supported;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &query);

    // Enable only what is supported
    if (supported.shaderSampledImageArrayNonUniformIndexing)
        descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

    if (supported.runtimeDescriptorArray) descriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;

    if (supported.descriptorBindingVariableDescriptorCount)
        descriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;

    if (supported.descriptorBindingPartiallyBound) descriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;

    if (supported.descriptorBindingStorageImageUpdateAfterBind)
        descriptorIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;

    if (supported.descriptorBindingUniformBufferUpdateAfterBind)
        descriptorIndexingFeatures.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;

    if (supported.descriptorBindingStorageBufferUpdateAfterBind)
        descriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;

    if (supported.descriptorBindingSampledImageUpdateAfterBind)
        descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;

    descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

    s_appendToPNextChain(featuresToEnable, reinterpret_cast<VkBaseOutStructure *>(&descriptorIndexingFeatures));
}

static bool s_enableVertexInputDynamicState(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 &featuresToEnable,
                                            VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT &outFeatures)
{
    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT supported{};
    supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT;

    VkPhysicalDeviceFeatures2 query{};
    query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    query.pNext = &supported;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &query);

    if (supported.vertexInputDynamicState) {
        outFeatures = {};
        outFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT;
        outFeatures.vertexInputDynamicState = VK_TRUE;

        s_appendToPNextChain(featuresToEnable, reinterpret_cast<VkBaseOutStructure *>(&outFeatures));

        RP_CORE_INFO("vertexInputDynamicState enabled.");
    } else {
        RP_CORE_WARN("vertexInputDynamicState NOT supported.");
        return false;
    }
    return true;
}

static bool s_enableVertexAttributeRobustness(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 &featuresToEnable,
                                              VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT &outFeatures)
{
    VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT supported{};
    supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_ROBUSTNESS_FEATURES_EXT;

    VkPhysicalDeviceFeatures2 query{};
    query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    query.pNext = &supported;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &query);

    if (supported.vertexAttributeRobustness) {
        outFeatures = {};
        outFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_ROBUSTNESS_FEATURES_EXT;
        outFeatures.vertexAttributeRobustness = VK_TRUE;

        s_appendToPNextChain(featuresToEnable, reinterpret_cast<VkBaseOutStructure *>(&outFeatures));

        RP_CORE_INFO("EXT::vertexAttributeRobustness enabled.");
    } else {
        RP_CORE_WARN("EXT::vertexAttributeRobustness NOT supported.");
        return false;
    }
    return true;
}

static bool s_enableDynamicRendering(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 &featuresToEnable,
                                     VkPhysicalDeviceDynamicRenderingFeaturesKHR &outFeatures)
{
    VkPhysicalDeviceDynamicRenderingFeaturesKHR supported{};
    supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;

    VkPhysicalDeviceFeatures2 query{};
    query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    query.pNext = &supported;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &query);

    if (supported.dynamicRendering) {
        outFeatures = {};
        outFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
        outFeatures.dynamicRendering = VK_TRUE;

        s_appendToPNextChain(featuresToEnable, reinterpret_cast<VkBaseOutStructure *>(&outFeatures));

        RP_CORE_INFO("KHR::dynamicRendering enabled.");
    } else {
        RP_CORE_WARN("KHR::dynamicRendering NOT supported.");
        return false;
    }
    return true;
}

static void s_enableRobustness2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 &featuresToEnable,
                                VkPhysicalDeviceRobustness2FeaturesEXT &outFeatures)
{
    VkPhysicalDeviceRobustness2FeaturesEXT supported{};
    supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;

    VkPhysicalDeviceFeatures2 query{};
    query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    query.pNext = &supported;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &query);

    if (supported.nullDescriptor) {
        outFeatures = {};
        outFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
        outFeatures.nullDescriptor = VK_TRUE;

        s_appendToPNextChain(featuresToEnable, reinterpret_cast<VkBaseOutStructure *>(&outFeatures));

        RP_CORE_INFO("KHR::robustness2::nullDescriptor enabled.");
    } else {
        RP_CORE_WARN("KHR::robustness2::nullDescriptor NOT supported.");
    }
}

static void s_enableMultiview(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 &featuresToEnable,
                              VkPhysicalDeviceMultiviewFeaturesKHR &outFeatures)
{
    VkPhysicalDeviceMultiviewFeaturesKHR supported{};
    supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHR;

    VkPhysicalDeviceFeatures2 query{};
    query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    query.pNext = &supported;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &query);

    if (supported.multiview) {
        outFeatures = {};
        outFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHR;
        outFeatures.multiview = VK_TRUE;

        s_appendToPNextChain(featuresToEnable, reinterpret_cast<VkBaseOutStructure *>(&outFeatures));

        RP_CORE_INFO("KHR::multiview enabled.");
    } else {
        RP_CORE_WARN("KHR::multiview NOT supported.");
    }
}

static bool s_enableRayTracingFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 &featuresToEnable,
                                       VkPhysicalDeviceBufferDeviceAddressFeatures &outBDA,
                                       VkPhysicalDeviceAccelerationStructureFeaturesKHR &outAS,
                                       VkPhysicalDeviceRayTracingPipelineFeaturesKHR &outRTP,
                                       VkPhysicalDeviceRayQueryFeaturesKHR &outRQ)
{
    // --- Query support ---
    VkPhysicalDeviceBufferDeviceAddressFeatures supportedBDA{};
    supportedBDA.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR supportedAS{};
    supportedAS.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR supportedRTP{};
    supportedRTP.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;

    VkPhysicalDeviceRayQueryFeaturesKHR supportedRQ{};
    supportedRQ.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;

    VkPhysicalDeviceFeatures2 query{};
    query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    // Chain all queries at once
    supportedBDA.pNext = &supportedAS;
    supportedAS.pNext = &supportedRTP;
    supportedRTP.pNext = &supportedRQ;
    query.pNext = &supportedBDA;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &query);

    // --- Hard requirements ---
    const bool supported = supportedBDA.bufferDeviceAddress && supportedAS.accelerationStructure &&
                           supportedRTP.rayTracingPipeline && supportedRQ.rayQuery;

    if (!supported) {
        RP_CORE_WARN("Ray tracing NOT supported on this device.");
        return false;
    }

    RP_CORE_INFO("Ray tracing supported. Enabling features.");

    // --- Enable features ---
    outBDA = {};
    outBDA.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    outBDA.bufferDeviceAddress = VK_TRUE;

    outAS = {};
    outAS.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    outAS.accelerationStructure = VK_TRUE;
    outAS.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;

    outRTP = {};
    outRTP.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    outRTP.rayTracingPipeline = VK_TRUE;

    outRQ = {};
    outRQ.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    outRQ.rayQuery = VK_TRUE;

    // --- Chain in correct dependency order ---
    s_appendToPNextChain(featuresToEnable, reinterpret_cast<VkBaseOutStructure *>(&outBDA));
    s_appendToPNextChain(featuresToEnable, reinterpret_cast<VkBaseOutStructure *>(&outAS));
    s_appendToPNextChain(featuresToEnable, reinterpret_cast<VkBaseOutStructure *>(&outRTP));
    s_appendToPNextChain(featuresToEnable, reinterpret_cast<VkBaseOutStructure *>(&outRQ));

    return true;
}

static bool s_enableTimelineSemaphores(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 &featuresToEnable,
                                       VkPhysicalDeviceTimelineSemaphoreFeatures &outFeatures)
{
    VkPhysicalDeviceTimelineSemaphoreFeatures supported{};
    supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;

    VkPhysicalDeviceFeatures2 query{};
    query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    query.pNext = &supported;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &query);

    if (!supported.timelineSemaphore) {
        RP_CORE_WARN("Timeline semaphores NOT supported.");
        return false;
    }

    outFeatures = {};
    outFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    outFeatures.timelineSemaphore = VK_TRUE;

    s_appendToPNextChain(featuresToEnable, reinterpret_cast<VkBaseOutStructure *>(&outFeatures));

    RP_CORE_INFO("Timeline semaphores enabled.");
    return true;
}

template <typename T> static bool s_loadDeviceFunction(VkDevice device, const char *name, T &outFn)
{
    outFn = reinterpret_cast<T>(vkGetDeviceProcAddr(device, name));
    return outFn != nullptr;
}

static void s_loadVertexInputDynamicState(VkDevice device, bool enabled, PFN_vkCmdSetVertexInputEXT &fn, bool &outEnabled)
{
    if (!enabled) {
        outEnabled = false;
        return;
    }

    outEnabled = s_loadDeviceFunction(device, "vkCmdSetVertexInputEXT", fn);

    if (!outEnabled) RP_CORE_ERROR("Failed to load vkCmdSetVertexInputEXT");
}

static void s_loadDynamicRendering(VkDevice device, bool enabled, PFN_vkCmdBeginRenderingKHR &beginFn,
                                   PFN_vkCmdEndRenderingKHR &endFn, bool &outEnabled)
{
    if (!enabled) {
        outEnabled = false;
        return;
    }

    const bool ok = s_loadDeviceFunction(device, "vkCmdBeginRenderingKHR", beginFn) &&
                    s_loadDeviceFunction(device, "vkCmdEndRenderingKHR", endFn);

    outEnabled = ok;

    if (!ok) RP_CORE_ERROR("Failed to load dynamic rendering functions");
}

static void s_loadMultiDraw(VkDevice device, PFN_vkCmdDrawMultiEXT &draw, PFN_vkCmdDrawMultiIndexedEXT &drawIndexed)
{
    s_loadDeviceFunction(device, "vkCmdDrawMultiEXT", draw);
    s_loadDeviceFunction(device, "vkCmdDrawMultiIndexedEXT", drawIndexed);
}

static bool
s_loadRayTracing(VkPhysicalDevice physicalDevice, VkDevice device, PFN_vkCreateAccelerationStructureKHR &createAS,
                 PFN_vkDestroyAccelerationStructureKHR &destroyAS, PFN_vkGetAccelerationStructureBuildSizesKHR &getBuildSizes,
                 PFN_vkCmdBuildAccelerationStructuresKHR &cmdBuild, PFN_vkGetAccelerationStructureDeviceAddressKHR &getAddress,
                 PFN_vkCreateRayTracingPipelinesKHR &createPipelines, PFN_vkGetRayTracingShaderGroupHandlesKHR &getHandles,
                 PFN_vkCmdTraceRaysKHR &traceRays, VkPhysicalDeviceRayTracingPipelinePropertiesKHR &rtProps,
                 VkPhysicalDeviceAccelerationStructurePropertiesKHR &asProps)
{
    const bool ok = s_loadDeviceFunction(device, "vkCreateAccelerationStructureKHR", createAS) &&
                    s_loadDeviceFunction(device, "vkDestroyAccelerationStructureKHR", destroyAS) &&
                    s_loadDeviceFunction(device, "vkGetAccelerationStructureBuildSizesKHR", getBuildSizes) &&
                    s_loadDeviceFunction(device, "vkCmdBuildAccelerationStructuresKHR", cmdBuild) &&
                    s_loadDeviceFunction(device, "vkGetAccelerationStructureDeviceAddressKHR", getAddress) &&
                    s_loadDeviceFunction(device, "vkCreateRayTracingPipelinesKHR", createPipelines) &&
                    s_loadDeviceFunction(device, "vkGetRayTracingShaderGroupHandlesKHR", getHandles) &&
                    s_loadDeviceFunction(device, "vkCmdTraceRaysKHR", traceRays);

    if (!ok) return false;

    rtProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    asProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;

    VkPhysicalDeviceProperties2 props{};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = &rtProps;
    rtProps.pNext = &asProps;

    vkGetPhysicalDeviceProperties2(physicalDevice, &props);

    return true;
}

static void s_createQueues(VkDevice device, const QueueFamilyIndices &indices,
                           std::map<uint32_t, std::shared_ptr<VulkanQueue>> &queues, std::shared_ptr<VulkanQueue> &vendorQueue)
{

    std::set<uint32_t> uniqueQueueFamilies = {indices.familyIndices[GRAPHICS], indices.familyIndices[PRESENT],
                                              indices.familyIndices[COMPUTE], indices.familyIndices[TRANSFER]};

    for (uint32_t queueFamily : uniqueQueueFamilies) {
        if (queueFamily == UINT32_MAX) continue;

        std::vector<std::string> queueTypeNames;

        bool hasGraphics = (indices.familyIndices[GRAPHICS] == queueFamily);
        bool hasPresent = (indices.familyIndices[PRESENT] == queueFamily);
        bool hasCompute = (indices.familyIndices[COMPUTE] == queueFamily);
        bool hasTransfer = (indices.familyIndices[TRANSFER] == queueFamily);

        if (hasGraphics) queueTypeNames.push_back("graphics");
        if (hasPresent) queueTypeNames.push_back("present");
        if (hasCompute) queueTypeNames.push_back("compute");
        if (hasTransfer) queueTypeNames.push_back("transfer");

        std::string queueName;
        for (size_t i = 0; i < queueTypeNames.size(); ++i) {
            if (i > 0) queueName += "|";
            queueName += queueTypeNames[i];
        }

        uint32_t queueCount = 0;
        for (int i = 0; i < COUNT; ++i) {
            if (indices.familyIndices[i] == queueFamily) {
                queueCount = indices.familyQueueCounts[i];
                break;
            }
        }

        if (queueCount == 0) continue;

        auto primaryQueue = std::make_shared<VulkanQueue>(device, queueFamily, 0, queueName);
        queues[queueFamily] = primaryQueue;

        if (hasGraphics && queueCount > 1 && vendorQueue == nullptr) {
            vendorQueue = std::make_shared<VulkanQueue>(device, queueFamily, 1, "vendor");
        }
    }
}

void VulkanContext::createLogicalDevice()
{

    m_queueFamilyIndices = findQueueFamilies(m_physicalDevice);
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        m_queueFamilyIndices.familyIndices[GRAPHICS], m_queueFamilyIndices.familyIndices[PRESENT],
        m_queueFamilyIndices.familyIndices[COMPUTE], m_queueFamilyIndices.familyIndices[TRANSFER]};

    std::vector<std::vector<float>> queuePriorityVectors;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        if (queueFamily == UINT32_MAX) continue;

        uint32_t queueCount = 0;
        for (int i = 0; i < COUNT; ++i) {
            if (m_queueFamilyIndices.familyIndices[i] == queueFamily) {
                queueCount = m_queueFamilyIndices.familyQueueCounts[i];
                break;
            }
        }

        std::vector<float> priorities;
        if (queueCount > 0) {
            priorities.resize(queueCount);
            if (queueCount == 1) {
                priorities[0] = 1.0f;
            } else {
                for (uint32_t i = 0; i < queueCount; ++i) {
                    priorities[i] = 1.0f - (static_cast<float>(i) / static_cast<float>(queueCount - 1));
                }
            }
        }

        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = queueCount;
        if (!priorities.empty()) {
            queuePriorityVectors.push_back(std::move(priorities));
            queueCreateInfo.pQueuePriorities = queuePriorityVectors.back().data();
        } else {
            queueCreateInfo.pQueuePriorities = nullptr;
        }

        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures2 physicalDeviceFeaturesToEnable{};
    physicalDeviceFeaturesToEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    s_enableCoreFeatures(m_physicalDevice, physicalDeviceFeaturesToEnable);

    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{};
    s_enableDescriptorIndexingFeatures(m_physicalDevice, physicalDeviceFeaturesToEnable, descriptorIndexingFeatures);

    VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemaphores{};
    s_enableTimelineSemaphores(m_physicalDevice, physicalDeviceFeaturesToEnable, timelineSemaphores);

    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vertexInputDynamic{};
    m_isVertexInputDynamicStateEnabled =
        s_enableVertexInputDynamicState(m_physicalDevice, physicalDeviceFeaturesToEnable, vertexInputDynamic);

    VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT vertexAttribRobust{};
    m_isVertexAttributeRobustnessEnabled =
        s_enableVertexAttributeRobustness(m_physicalDevice, physicalDeviceFeaturesToEnable, vertexAttribRobust);

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering{};
    m_isDynamicRenderingEnabled = s_enableDynamicRendering(m_physicalDevice, physicalDeviceFeaturesToEnable, dynamicRendering);

    VkPhysicalDeviceRobustness2FeaturesEXT robustness2{};
    s_enableRobustness2(m_physicalDevice, physicalDeviceFeaturesToEnable, robustness2);

    VkPhysicalDeviceMultiviewFeaturesKHR multiview{};
    s_enableMultiview(m_physicalDevice, physicalDeviceFeaturesToEnable, multiview);

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipeline{};
    VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{};
    m_isRayTracingEnabled = s_enableRayTracingFeatures(m_physicalDevice, physicalDeviceFeaturesToEnable, bufferDeviceAddress,
                                                       accelerationStructure, rayTracingPipeline, rayQuery);

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();

    createInfo.pNext = &physicalDeviceFeaturesToEnable;
    createInfo.pEnabledFeatures = nullptr;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
        throw std::runtime_error("Failed to create logical device");

    s_loadVertexInputDynamicState(m_device, vertexInputDynamic.vertexInputDynamicState, vkCmdSetVertexInputEXT,
                                  m_isVertexInputDynamicStateEnabled);
    s_loadDynamicRendering(m_device, dynamicRendering.dynamicRendering, vkCmdBeginRenderingKHR, vkCmdEndRenderingKHR,
                           m_isDynamicRenderingEnabled);
    s_loadMultiDraw(m_device, vkCmdDrawMultiEXT, vkCmdDrawMultiIndexedEXT);
    s_loadRayTracing(m_physicalDevice, m_device, vkCreateAccelerationStructureKHR, vkDestroyAccelerationStructureKHR,
                     vkGetAccelerationStructureBuildSizesKHR, vkCmdBuildAccelerationStructuresKHR,
                     vkGetAccelerationStructureDeviceAddressKHR, vkCreateRayTracingPipelinesKHR,
                     vkGetRayTracingShaderGroupHandlesKHR, vkCmdTraceRaysKHR, m_rayTracingPipelineProperties,
                     m_accelerationStructureProperties);

    s_createQueues(m_device, m_queueFamilyIndices, m_queues, m_vendorQueue);

    RP_CORE_INFO("Logical device created successfully!");
}

void VulkanContext::createWindowsSurface(WindowContext *windowContext)
{
#ifdef RAPTURE_RUNTIME_WAYLAND_DETECTION
    bool usingWayland = isWaylandSession();
    if (usingWayland) {
        RP_CORE_INFO("Creating Wayland surface...");
    } else {
        RP_CORE_INFO("Creating X11 surface...");
    }
#endif

    if (glfwCreateWindowSurface(m_instance, (GLFWwindow *)windowContext->getNativeWindowContext(), nullptr, &m_surface) !=
        VK_SUCCESS) {
#ifdef RAPTURE_RUNTIME_WAYLAND_DETECTION
        if (usingWayland) {
            RP_CORE_ERROR("Failed to create Wayland window surface!");
            throw std::runtime_error("Failed to create Wayland window surface!");
        } else {
            RP_CORE_ERROR("Failed to create X11 window surface!");
            throw std::runtime_error("Failed to create X11 window surface!");
        }
#else
        RP_CORE_ERROR("Failed to create window surface!");
        throw std::runtime_error("Failed to create window surface!");
#endif
    }

#ifdef RAPTURE_RUNTIME_WAYLAND_DETECTION
    if (usingWayland) {
        RP_CORE_INFO("Wayland surface created successfully!");
    } else {
        RP_CORE_INFO("X11 surface created successfully!");
    }
#else
    RP_CORE_INFO("Window surface created successfully!");
#endif
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

} // namespace Rapture

#include "DescriptorArrayManager.h"
#include "Logging/Log.h"
#include "WindowContext/Application.h"

namespace Rapture {

// Unified descriptor set management
VkDescriptorSetLayout DescriptorArrayManager::m_unifiedLayout = VK_NULL_HANDLE;
VkDescriptorSet DescriptorArrayManager::m_unifiedSet = VK_NULL_HANDLE;
VkDescriptorPool DescriptorArrayManager::m_descriptorPool = VK_NULL_HANDLE;

// Individual arrays
std::shared_ptr<TextureDescriptorArray> DescriptorArrayManager::m_textureArray;
std::unordered_map<DescriptorArrayType, std::shared_ptr<BufferDescriptorArray>> DescriptorArrayManager::m_bufferArrays;

void DescriptorArrayManager::init(std::vector<DescriptorArrayConfig> configs) {
    RP_CORE_INFO("Initializing descriptor array manager");
    
    // Clear existing arrays
    m_textureArray.reset();
    m_bufferArrays.clear();

    createDescriptorPools();
    createUnifiedDescriptorSet(configs);
    
    for (const auto& config : configs) {
        switch (config.arrayType) {
            case DescriptorArrayType::TEXTURE:
                if (!m_textureArray) {
                    m_textureArray = std::make_shared<TextureDescriptorArray>(config, m_unifiedSet);
                    RP_CORE_INFO("Created texture descriptor array with capacity {}", config.capacity);
                } else {
                    RP_CORE_WARN("Texture descriptor array already exists, you should only provide 1 config per type! skipping duplicate configuration");
                }
                break;
                
            case DescriptorArrayType::STORAGE_BUFFER:
            case DescriptorArrayType::UNIFORM_BUFFER:
                if (m_bufferArrays.find(config.arrayType) == m_bufferArrays.end()) {
                    m_bufferArrays[config.arrayType] = std::make_shared<BufferDescriptorArray>(config, m_unifiedSet);
                    RP_CORE_INFO("Created {} descriptor array with capacity {}", 
                                config.arrayType == DescriptorArrayType::STORAGE_BUFFER ? "storage buffer" : "uniform buffer",
                                config.capacity);
                } else {
                    RP_CORE_WARN("Descriptor array of type {} already exists, skipping duplicate configuration", 
                                static_cast<int>(config.arrayType));
                }
                break;
                
            default:
                RP_CORE_WARN("Unknown descriptor array type: {}", static_cast<int>(config.arrayType));
                break;
        }
    }
}

void DescriptorArrayManager::shutdown() {
    RP_CORE_INFO("Shutting down descriptor array manager");
    m_textureArray.reset();
    m_bufferArrays.clear();

    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();

    // Clean up unified descriptor set resources
    if (m_unifiedLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanContext.getLogicalDevice(), m_unifiedLayout, nullptr);
        m_unifiedLayout = VK_NULL_HANDLE;
    }

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanContext.getLogicalDevice(), m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    m_unifiedSet = VK_NULL_HANDLE; // Set is destroyed with pool
}

std::shared_ptr<TextureDescriptorArray> DescriptorArrayManager::getTextureArray() {
    if (!m_textureArray) {
        RP_CORE_WARN("Texture descriptor array not initialized");
    }
    return m_textureArray;
}

std::shared_ptr<BufferDescriptorArray> DescriptorArrayManager::getBufferArray(DescriptorArrayType type) {
    auto it = m_bufferArrays.find(type);
    if (it != m_bufferArrays.end()) {
        return it->second;
    }
    
    RP_CORE_WARN("Descriptor array of type {} not found or not initialized", static_cast<int>(type));
    return nullptr;
}

std::unique_ptr<DescriptorSubAllocationBase<Texture>> DescriptorArrayManager::createTextureSubAllocation(uint32_t capacity, std::string name) {
    if (!m_textureArray) {
        RP_CORE_ERROR("Cannot create texture sub-allocation: texture descriptor array not initialized");
        return nullptr;
    }
    
    return m_textureArray->createSubAllocation(capacity, name);
}

std::unique_ptr<DescriptorSubAllocationBase<Buffer>> DescriptorArrayManager::createStorageSubAllocation(DescriptorArrayType type, uint32_t capacity, std::string name) {
    auto bufferArray = getBufferArray(type);
    if (!bufferArray) {
        RP_CORE_ERROR("Cannot create storage sub-allocation: descriptor array of type {} not initialized", static_cast<int>(type));
        return nullptr;
    }
    
    RP_CORE_INFO("Creating storage sub-allocation for {} with capacity {}", name, capacity);
    return bufferArray->createSubAllocation(capacity, name);
}

void DescriptorArrayManager::createDescriptorPools() {
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto descriptorIndexingFeatures = vulkanContext.getDescriptorIndexingFeatures();
    
    // Create pool sizes for all possible descriptor types
    // We'll use generous sizes to accommodate all arrays
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10000},  // For textures
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5000},           // For storage buffers
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5000}            // For uniform buffers
    };
    
    VkDescriptorPoolCreateFlags poolFlags = 0;
    
    // Check if we can use update after bind for any descriptor type
    bool canUseUpdateAfterBind = descriptorIndexingFeatures.descriptorBindingUpdateUnusedWhilePending &&
        (descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind ||
         descriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind ||
         descriptorIndexingFeatures.descriptorBindingUniformBufferUpdateAfterBind);
    
    if (canUseUpdateAfterBind) {
        poolFlags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        RP_CORE_INFO("Using VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT for descriptor array pool");
    }
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = poolFlags;
    poolInfo.maxSets = 10; // Allow for multiple descriptor sets
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    
    if (vkCreateDescriptorPool(vulkanContext.getLogicalDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create descriptor pool for DescriptorArrayManager.");
        throw std::runtime_error("Failed to create descriptor pool for DescriptorArrayManager.");
    }
    
    RP_CORE_INFO("Created shared descriptor pool for all descriptor arrays");
}

void DescriptorArrayManager::createUnifiedDescriptorSet(const std::vector<DescriptorArrayConfig>& configs) {
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto descriptorIndexingFeatures = vulkanContext.getDescriptorIndexingFeatures();
    
    // Create bindings for all configured arrays
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkDescriptorBindingFlags> bindingFlags;
    
    bool useUpdateAfterBind = false;
    
    for (const auto& config : configs) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = config.bindingIndex;
        binding.descriptorType = config.getTypeVK();
        binding.descriptorCount = config.capacity;
        binding.stageFlags = VK_SHADER_STAGE_ALL;
        bindings.push_back(binding);
        
        // Set binding flags for this descriptor type
        VkDescriptorBindingFlags flags = 0;
        
        if (descriptorIndexingFeatures.descriptorBindingPartiallyBound) {
            flags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
        }
        
        // Check update after bind support for each type
        bool typeSupportsUpdateAfterBind = descriptorIndexingFeatures.descriptorBindingUpdateUnusedWhilePending;
        
        switch (config.getTypeVK()) {
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                typeSupportsUpdateAfterBind = typeSupportsUpdateAfterBind && 
                    descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind;
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                typeSupportsUpdateAfterBind = typeSupportsUpdateAfterBind && 
                    descriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind;
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                typeSupportsUpdateAfterBind = typeSupportsUpdateAfterBind && 
                    descriptorIndexingFeatures.descriptorBindingUniformBufferUpdateAfterBind;
                break;
        }
        
        if (typeSupportsUpdateAfterBind) {
            flags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            useUpdateAfterBind = true;
        }
        
        bindingFlags.push_back(flags);
        
        RP_CORE_INFO("Added binding {} for {} with capacity {}", 
                    config.bindingIndex, 
                    config.arrayType == DescriptorArrayType::TEXTURE ? "textures" : 
                    (config.arrayType == DescriptorArrayType::STORAGE_BUFFER ? "storage buffers" : "uniform buffers"),
                    config.capacity);
    }
    
    // Create descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    VkDescriptorSetLayoutBindingFlagsCreateInfo extendedInfo{};
    if (!bindingFlags.empty()) {
        extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        extendedInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        extendedInfo.pBindingFlags = bindingFlags.data();
        layoutInfo.pNext = &extendedInfo;
        
        if (useUpdateAfterBind) {
            layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        }
    }
    
    if (vkCreateDescriptorSetLayout(vulkanContext.getLogicalDevice(), &layoutInfo, nullptr, &m_unifiedLayout) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create unified descriptor set layout.");
        throw std::runtime_error("Failed to create unified descriptor set layout.");
    }
    
    // Allocate unified descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_unifiedLayout;
    
    if (vkAllocateDescriptorSets(vulkanContext.getLogicalDevice(), &allocInfo, &m_unifiedSet) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to allocate unified descriptor set.");
        throw std::runtime_error("Failed to allocate unified descriptor set.");
    }
    
    RP_CORE_INFO("Created unified descriptor set with {} bindings", bindings.size());
}

} // namespace Rapture 
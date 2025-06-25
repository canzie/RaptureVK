#include "DescriptorSet.h"
#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include "Buffers/Buffers.h"
#include "Textures/Texture.h"
#include "AccelerationStructures/TLAS.h"

#include "Buffers/Descriptors/DescriptorBinding.h"

#include <stdexcept>
#include <algorithm>
#include <array>

namespace Rapture {

// Static member definitions
VkDescriptorPool DescriptorSet::s_pool = VK_NULL_HANDLE;
uint32_t DescriptorSet::s_poolRefCount = 0;
uint32_t DescriptorSet::s_poolBufferCount = 0;
uint32_t DescriptorSet::s_poolTextureCount = 0;
uint32_t DescriptorSet::s_poolStorageBufferCount = 0;
uint32_t DescriptorSet::s_poolStorageImageCount = 0;
uint32_t DescriptorSet::s_poolInputAttachmentCount = 0;
uint32_t DescriptorSet::s_poolAccelerationStructureCount = 0;

// Static const member definitions
const uint32_t DescriptorSet::s_maxSets;
const uint32_t DescriptorSet::s_maxBuffers;
const uint32_t DescriptorSet::s_maxTextures;
const uint32_t DescriptorSet::s_maxStorageBuffers;
const uint32_t DescriptorSet::s_maxStorageImages;
const uint32_t DescriptorSet::s_maxInputAttachments;
const uint32_t DescriptorSet::s_maxAccelerationStructures;

DescriptorSet::DescriptorSet(const DescriptorSetBindings& bindings)
    : m_layout(VK_NULL_HANDLE), m_set(VK_NULL_HANDLE), m_setNumber(bindings.setNumber) {
    
    auto& app = Application::getInstance();
    m_device = app.getVulkanContext().getLogicalDevice();

    // Increment reference count and create pool if needed
    s_poolRefCount++;
    if (s_pool == VK_NULL_HANDLE) {
        createDescriptorPool();
    }

    if (s_poolRefCount > s_maxSets) {
        RP_CORE_ERROR("DescriptorSet::DescriptorSet - too many descriptor sets! Current: {}, Max: {}", 
                      s_poolRefCount, s_maxSets);
        throw std::runtime_error("DescriptorSet::DescriptorSet - too many descriptor sets!");
    }

    // Check if we still have space in the pool before proceeding
    try {
        updateUsedCounts(bindings);
    } catch (const std::exception& e) {
        RP_CORE_ERROR("DescriptorSet::DescriptorSet - Failed to allocate descriptors: {}", e.what());
        // Decrement ref count since we're failing to create
        s_poolRefCount--;
        throw;
    }


    createDescriptorSetLayout(bindings);
    createDescriptorSet();
    for (const auto& binding : bindings.bindings) {
        createBinding(binding);
    }

    

}

DescriptorSet::~DescriptorSet() {
    // Layout is managed externally (by shader reflection), so we don't destroy it
    
    // Decrement counters
    s_poolBufferCount -= m_usedBuffers;
    s_poolTextureCount -= m_usedTextures;
    s_poolStorageBufferCount -= m_usedStorageBuffers;
    s_poolStorageImageCount -= m_usedStorageImages;
    s_poolInputAttachmentCount -= m_usedInputAttachments;
    
    // Decrement reference count and destroy pool if no more references
    s_poolRefCount--;
    if (s_poolRefCount == 0 && s_pool != VK_NULL_HANDLE) {
        destroyDescriptorPool();
    }
    
    // Descriptor set is automatically freed when pool is destroyed/reset
}

void DescriptorSet::bind(VkCommandBuffer commandBuffer, std::shared_ptr<PipelineBase> pipeline) {
    vkCmdBindDescriptorSets(commandBuffer, pipeline->getPipelineBindPoint(), pipeline->getPipelineLayoutVk(), m_setNumber, 1, &m_set, 0, nullptr);
}

void DescriptorSet::createBinding(const DescriptorSetBinding &binding)
{

    uint32_t bindNumber = getBindingBindNumber(binding.location);

    switch (binding.type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            m_uniformBufferBindings[binding.location] = std::make_shared<DescriptorBindingUniformBuffer>(this, bindNumber, binding.count);
            break;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            m_textureBindings[binding.location] = std::make_shared<DescriptorBindingTexture>(this, bindNumber, binding.viewType, binding.useStorageImageInfo, binding.count);
            break;
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            m_tlasBindings[binding.location] = std::make_shared<DescriptorBindingTLAS>(this, bindNumber, binding.count);
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            m_ssboBindings[binding.location] = std::make_shared<DescriptorBindingSSBO>(this, bindNumber, binding.count);
            break;
        default:
            RP_CORE_ERROR("DescriptorSet::createBinding - unknown descriptor type: {}", static_cast<int>(binding.type));
            return;
    }
}

void DescriptorSet::createDescriptorSetLayout(const DescriptorSetBindings &bindings) {
    
    Application& app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    std::vector<VkDescriptorBindingFlags> bindingFlags;
    layoutBindings.reserve(bindings.bindings.size());
    bindingFlags.reserve(bindings.bindings.size());

    // each binding in a set
    for (const auto& bindingInfo : bindings.bindings) {
        uint32_t bindNumber = getBindingBindNumber(bindingInfo.location);
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = bindNumber;
        layoutBinding.descriptorType = bindingInfo.type;
        layoutBinding.descriptorCount = bindingInfo.count;
        layoutBinding.stageFlags = VK_SHADER_STAGE_ALL;
        layoutBinding.pImmutableSamplers = nullptr;

        layoutBindings.push_back(layoutBinding);
        
        // Add UPDATE_AFTER_BIND flag to allow updating descriptors while bound to pending command buffers
        bindingFlags.push_back(VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
    }

    // Set up binding flags info
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
    bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;  // Required for UPDATE_AFTER_BIND
    layoutInfo.pNext = &bindingFlagsInfo;  // Chain the binding flags
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();

    // layout for all bindings in a set

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create descriptor set layout for set {0}!", m_setNumber);
        throw std::runtime_error("Failed to create descriptor set layout!");
    }
}

void DescriptorSet::createDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = s_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_layout;

    VkResult result = vkAllocateDescriptorSets(m_device, &allocInfo, &m_set);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to allocate descriptor set for set {}! VkResult: {} ({})", 
                      m_setNumber, static_cast<int>(result), 
                      result == VK_ERROR_OUT_OF_POOL_MEMORY ? "VK_ERROR_OUT_OF_POOL_MEMORY" :
                      result == VK_ERROR_FRAGMENTED_POOL ? "VK_ERROR_FRAGMENTED_POOL" : "OTHER");
        RP_CORE_ERROR("Pool status - Sets: {}/{}, Buffers: {}/{}, Textures: {}/{}, "
                      "StorageBuffers: {}/{}, StorageImages: {}/{}, AccelStructs: {}/{}", 
                      s_poolRefCount, s_maxSets,
                      s_poolBufferCount, s_maxBuffers,
                      s_poolTextureCount, s_maxTextures,
                      s_poolStorageBufferCount, s_maxStorageBuffers,
                      s_poolStorageImageCount, s_maxStorageImages,
                      s_poolAccelerationStructureCount, s_maxAccelerationStructures);
        throw std::runtime_error("Failed to allocate descriptor set");
    }
}



void DescriptorSet::createDescriptorPool()
{
    // Define pool sizes for different descriptor types
    std::array<VkDescriptorPoolSize, 6> poolSizes{};
    
    // Uniform buffers
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = s_maxBuffers;
    
    // Combined image samplers (textures)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = s_maxTextures;
    
    // Storage buffers
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = s_maxStorageBuffers;
    
    // Storage images
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[3].descriptorCount = s_maxStorageImages;
    
    // Input attachments
    poolSizes[4].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    poolSizes[4].descriptorCount = s_maxInputAttachments;

    // TLAS
    poolSizes[5].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[5].descriptorCount = s_maxAccelerationStructures;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = s_maxSets;

    VkResult result = vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &s_pool);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create descriptor pool! VkResult: {}", static_cast<int>(result));
        throw std::runtime_error("Failed to create descriptor pool");
    }
    
    RP_CORE_INFO("Created descriptor pool with {} max sets", s_maxSets);
}




void DescriptorSet::updateUsedCounts(const DescriptorSetBindings &bindings)
{
    // Check if adding this descriptor set would exceed any limits
    uint32_t newBuffers = 0, newTextures = 0, newStorageBuffers = 0, newStorageImages = 0, newInputAttachments = 0, newAccelerationStructures = 0;
    
    for (const auto& binding : bindings.bindings) {
        switch (binding.type) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                newBuffers += binding.count;
                break;
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case VK_DESCRIPTOR_TYPE_SAMPLER:
                newTextures += binding.count;
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                newStorageBuffers += binding.count;
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                newStorageImages += binding.count;
                break;
            case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                newInputAttachments += binding.count;
                break;
            case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
                newAccelerationStructures += binding.count;
                break;
            default:
                RP_CORE_WARN("Unknown descriptor type: {}", static_cast<int>(binding.type));
                break;
        }
    }

    // Log current resource usage for debugging
    RP_CORE_TRACE("DescriptorSet: Resource usage for set {} - Buffers: {}, Textures: {}, "
                  "StorageBuffers: {}, StorageImages: {}, InputAttachments: {}, AccelStructs: {}",
                  bindings.setNumber, newBuffers, newTextures, newStorageBuffers, 
                  newStorageImages, newInputAttachments, newAccelerationStructures);

    // Check limits before allocating
    if (s_poolBufferCount + newBuffers > s_maxBuffers) {
        RP_CORE_ERROR("DescriptorSet: Uniform buffer limit exceeded for set {}! "
                      "Current: {}, Requested: {}, Total would be: {}, Max: {}", 
                      bindings.setNumber, s_poolBufferCount, newBuffers, 
                      s_poolBufferCount + newBuffers, s_maxBuffers);
        throw std::runtime_error("DescriptorSet: Uniform buffer limit exceeded! Current: " + 
                                std::to_string(s_poolBufferCount) + ", Requested: " + 
                                std::to_string(newBuffers) + ", Max: " + std::to_string(s_maxBuffers));
    }
    
    if (s_poolTextureCount + newTextures > s_maxTextures) {
        RP_CORE_ERROR("DescriptorSet: Texture/Sampler limit exceeded for set {}! "
                      "Current: {}, Requested: {}, Total would be: {}, Max: {}", 
                      bindings.setNumber, s_poolTextureCount, newTextures, 
                      s_poolTextureCount + newTextures, s_maxTextures);
        throw std::runtime_error("DescriptorSet: Texture/Sampler limit exceeded! Current: " + 
                                std::to_string(s_poolTextureCount) + ", Requested: " + 
                                std::to_string(newTextures) + ", Max: " + std::to_string(s_maxTextures));
    }
    
    if (s_poolStorageBufferCount + newStorageBuffers > s_maxStorageBuffers) {
        RP_CORE_ERROR("DescriptorSet: Storage buffer limit exceeded for set {}! "
                      "Current: {}, Requested: {}, Total would be: {}, Max: {}", 
                      bindings.setNumber, s_poolStorageBufferCount, newStorageBuffers, 
                      s_poolStorageBufferCount + newStorageBuffers, s_maxStorageBuffers);
        throw std::runtime_error("DescriptorSet: Storage buffer limit exceeded! Current: " + 
                                std::to_string(s_poolStorageBufferCount) + ", Requested: " + 
                                std::to_string(newStorageBuffers) + ", Max: " + std::to_string(s_maxStorageBuffers));
    }
    
    if (s_poolStorageImageCount + newStorageImages > s_maxStorageImages) {
        RP_CORE_ERROR("DescriptorSet: Storage image limit exceeded for set {}! "
                      "Current: {}, Requested: {}, Total would be: {}, Max: {}", 
                      bindings.setNumber, s_poolStorageImageCount, newStorageImages, 
                      s_poolStorageImageCount + newStorageImages, s_maxStorageImages);
        throw std::runtime_error("DescriptorSet: Storage image limit exceeded! Current: " + 
                                std::to_string(s_poolStorageImageCount) + ", Requested: " + 
                                std::to_string(newStorageImages) + ", Max: " + std::to_string(s_maxStorageImages));
    }
    
    if (s_poolInputAttachmentCount + newInputAttachments > s_maxInputAttachments) {
        RP_CORE_ERROR("DescriptorSet: Input attachment limit exceeded for set {}! "
                      "Current: {}, Requested: {}, Total would be: {}, Max: {}", 
                      bindings.setNumber, s_poolInputAttachmentCount, newInputAttachments, 
                      s_poolInputAttachmentCount + newInputAttachments, s_maxInputAttachments);
        throw std::runtime_error("DescriptorSet: Input attachment limit exceeded! Current: " + 
                                std::to_string(s_poolInputAttachmentCount) + ", Requested: " + 
                                std::to_string(newInputAttachments) + ", Max: " + std::to_string(s_maxInputAttachments));
    }

    if (s_poolAccelerationStructureCount + newAccelerationStructures > s_maxAccelerationStructures) {
        RP_CORE_ERROR("DescriptorSet: Acceleration structure limit exceeded for set {}! "
                      "Current: {}, Requested: {}, Total would be: {}, Max: {}", 
                      bindings.setNumber, s_poolAccelerationStructureCount, newAccelerationStructures, 
                      s_poolAccelerationStructureCount + newAccelerationStructures, s_maxAccelerationStructures);
        throw std::runtime_error("DescriptorSet: Acceleration structure limit exceeded! Current: " + 
                                std::to_string(s_poolAccelerationStructureCount) + ", Requested: " + 
                                std::to_string(newAccelerationStructures) + ", Max: " + std::to_string(s_maxAccelerationStructures));
    }

    // Update counters
    s_poolBufferCount += newBuffers;
    s_poolTextureCount += newTextures;
    s_poolStorageBufferCount += newStorageBuffers;
    s_poolStorageImageCount += newStorageImages;
    s_poolInputAttachmentCount += newInputAttachments;
    s_poolAccelerationStructureCount += newAccelerationStructures;

    // Store what this descriptor set is using for cleanup
    m_usedBuffers = newBuffers;
    m_usedTextures = newTextures;
    m_usedStorageBuffers = newStorageBuffers;
    m_usedStorageImages = newStorageImages;
    m_usedInputAttachments = newInputAttachments;
    m_usedAccelerationStructures = newAccelerationStructures;

    RP_CORE_INFO("DescriptorSet: Successfully allocated resources for set {} - "
                 "Pool usage: Sets {}/{}, Buffers {}/{}, Textures {}/{}, "
                 "StorageBuffers {}/{}, StorageImages {}/{}, AccelStructs {}/{}",
                 bindings.setNumber, s_poolRefCount, s_maxSets,
                 s_poolBufferCount, s_maxBuffers, s_poolTextureCount, s_maxTextures,
                 s_poolStorageBufferCount, s_maxStorageBuffers, s_poolStorageImageCount, s_maxStorageImages,
                 s_poolAccelerationStructureCount, s_maxAccelerationStructures);
}



void DescriptorSet::destroyDescriptorPool() {
    if (s_pool != VK_NULL_HANDLE) {
        auto& app = Application::getInstance();
        VkDevice device = app.getVulkanContext().getLogicalDevice();
        
        vkDestroyDescriptorPool(device, s_pool, nullptr);
        s_pool = VK_NULL_HANDLE;
        RP_CORE_INFO("Destroyed descriptor pool");
    }
}

} 
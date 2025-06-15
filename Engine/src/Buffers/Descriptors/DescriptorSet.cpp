#include "DescriptorSet.h"
#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include "Buffers/Buffers.h"
#include "Textures/Texture.h"
#include "AccelerationStructures/TLAS.h"

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
    : m_layout(bindings.layout), m_set(VK_NULL_HANDLE) {
    
    auto& app = Application::getInstance();
    m_device = app.getVulkanContext().getLogicalDevice();

    // Increment reference count and create pool if needed
    s_poolRefCount++;
    if (s_pool == VK_NULL_HANDLE) {
        createDescriptorPool();
    }

    if (s_poolRefCount > s_maxSets) {
        RP_CORE_ERROR("DescriptorSet::DescriptorSet - too many descriptor sets!");
        throw std::runtime_error("DescriptorSet::DescriptorSet - too many descriptor sets!");
    }

    // simply checks if we still have space in the pool. should never really exceed the limits
    // but i dont want to 😞🔫
    updateUsedCounts(bindings);



    // Allocate descriptor set from pool
    allocateDescriptorSet();
    
    // Write descriptor set data
    writeDescriptorSet(bindings);
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

void DescriptorSet::createDescriptorPool() {
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
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
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

void DescriptorSet::allocateDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = s_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_layout;

    {
        std::lock_guard<std::mutex> lock(m_descriptorUpdateMutex);

        VkResult result = vkAllocateDescriptorSets(m_device, &allocInfo, &m_set);
        if (result != VK_SUCCESS) {
            RP_CORE_ERROR("Failed to allocate descriptor set! VkResult: {}", static_cast<int>(result));
            throw std::runtime_error("Failed to allocate descriptor set");
        }
    }
}

void DescriptorSet::writeDescriptorSet(const DescriptorSetBindings& bindings) {
    std::vector<VkWriteDescriptorSet> descriptorWrites;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> accelerationStructureWrites;
    
    std::lock_guard<std::mutex> lock(m_descriptorUpdateMutex);


    // Reserve space to avoid reallocation
    descriptorWrites.reserve(bindings.bindings.size());
    bufferInfos.reserve(bindings.bindings.size());
    imageInfos.reserve(bindings.bindings.size());
    accelerationStructureWrites.reserve(bindings.bindings.size());

    for (const auto& binding : bindings.bindings) {
        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_set;
        descriptorWrite.dstBinding = binding.binding;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = binding.type;
        descriptorWrite.descriptorCount = binding.count;

        // TODO:: add a check for the correct uniform or storage bit
        if (std::holds_alternative<std::shared_ptr<Buffer>>(binding.resource)) {
            auto resource = std::get<std::shared_ptr<Buffer>>(binding.resource);

            if (resource) {
                bufferInfos.push_back(resource->getDescriptorBufferInfo());
                descriptorWrite.pBufferInfo = &bufferInfos.back();
            } else {
                RP_CORE_WARN("Buffer is null for binding {}", binding.binding);
                return; // Skip this binding
            }
        } else if (std::holds_alternative<std::shared_ptr<Texture>>(binding.resource)) {
            // Handle Texture
            auto resource = std::get<std::shared_ptr<Texture>>(binding.resource);
            if (resource) {
                if (binding.useStorageImageInfo) {
                    imageInfos.push_back(resource->getStorageImageDescriptorInfo());
                } else {
                    imageInfos.push_back(resource->getDescriptorImageInfo(binding.viewType));
                }
                descriptorWrite.pImageInfo = &imageInfos.back();
            } else {
                RP_CORE_WARN("Texture is null for binding {}", binding.binding);
                return; // Skip this binding
            }

        } else if (std::holds_alternative<std::reference_wrapper<TLAS>>(binding.resource)) {
            auto& resource = std::get<std::reference_wrapper<TLAS>>(binding.resource).get();
            if (resource.getAccelerationStructure() != VK_NULL_HANDLE) {
                // Create acceleration structure write descriptor
                VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureWrite{};
                accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                accelerationStructureWrite.accelerationStructureCount = 1;
                VkAccelerationStructureKHR accelerationStructure = resource.getAccelerationStructure();
                accelerationStructureWrite.pAccelerationStructures = &accelerationStructure;
                
                accelerationStructureWrites.push_back(accelerationStructureWrite);
                
                // Link the acceleration structure write to the main descriptor write
                descriptorWrite.pNext = &accelerationStructureWrites.back();
            } else {
                RP_CORE_WARN("TLAS is null for binding {}", binding.binding);
                return; // Skip this binding
            }
        }
        descriptorWrites.push_back(descriptorWrite);
    }

    if (!descriptorWrites.empty()) {
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()), 
                              descriptorWrites.data(), 0, nullptr);
    }
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

    // Check limits before allocating
    if (s_poolBufferCount + newBuffers > s_maxBuffers) {
        throw std::runtime_error("DescriptorSet: Uniform buffer limit exceeded! Current: " + 
                                std::to_string(s_poolBufferCount) + ", Requested: " + 
                                std::to_string(newBuffers) + ", Max: " + std::to_string(s_maxBuffers));
    }
    
    if (s_poolTextureCount + newTextures > s_maxTextures) {
        throw std::runtime_error("DescriptorSet: Texture/Sampler limit exceeded! Current: " + 
                                std::to_string(s_poolTextureCount) + ", Requested: " + 
                                std::to_string(newTextures) + ", Max: " + std::to_string(s_maxTextures));
    }
    
    if (s_poolStorageBufferCount + newStorageBuffers > s_maxStorageBuffers) {
        throw std::runtime_error("DescriptorSet: Storage buffer limit exceeded! Current: " + 
                                std::to_string(s_poolStorageBufferCount) + ", Requested: " + 
                                std::to_string(newStorageBuffers) + ", Max: " + std::to_string(s_maxStorageBuffers));
    }
    
    if (s_poolStorageImageCount + newStorageImages > s_maxStorageImages) {
        throw std::runtime_error("DescriptorSet: Storage image limit exceeded! Current: " + 
                                std::to_string(s_poolStorageImageCount) + ", Requested: " + 
                                std::to_string(newStorageImages) + ", Max: " + std::to_string(s_maxStorageImages));
    }
    
    if (s_poolInputAttachmentCount + newInputAttachments > s_maxInputAttachments) {
        throw std::runtime_error("DescriptorSet: Input attachment limit exceeded! Current: " + 
                                std::to_string(s_poolInputAttachmentCount) + ", Requested: " + 
                                std::to_string(newInputAttachments) + ", Max: " + std::to_string(s_maxInputAttachments));
    }

    if (s_poolAccelerationStructureCount + newAccelerationStructures > s_maxAccelerationStructures) {
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
}

void DescriptorSet::updateDescriptorSet(const DescriptorSetBindings& bindings) {
    writeDescriptorSet(bindings);
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
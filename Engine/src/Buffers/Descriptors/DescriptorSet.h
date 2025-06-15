#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <variant>
#include <memory>
#include <string>
#include <functional>
#include <mutex>

#include "Textures/TextureCommon.h"

namespace Rapture {

    // TODO create a caching system for descriptor sets
    // because right now we need the shader to give us the layout, which means each instance of a shader needs to create a new descriptor set
    // for a possible equal layout.
    // e.g. the gbuffer pass cant create the set because it does not have the layout, so the users need to create a set individually, leading to possible copies
    // we can fix this by using a cache system, this way we support both identical and slightly different ones.
    // we can go even further and log a warn when a layout can be optimised to be identical to a cached one.

// Forward declarations
class Buffer;
class Texture;
class TLAS;

struct DescriptorSetBinding {
    uint32_t binding;
    VkDescriptorType type;
    uint32_t count = 1;
    TextureViewType viewType = TextureViewType::DEFAULT;
    // Use variant to hold different resource types
    std::variant<std::shared_ptr<Buffer>, std::shared_ptr<Texture>, std::reference_wrapper<TLAS>> resource;
    bool useStorageImageInfo = false; // Flag to use storage image descriptor info

};

struct DescriptorSetBindings {
    std::vector<DescriptorSetBinding> bindings;
    VkDescriptorSetLayout layout;
};

class DescriptorSet {
public:
    DescriptorSet(const DescriptorSetBindings& bindings);
    ~DescriptorSet();

    // Getter for the descriptor set
    VkDescriptorSet getDescriptorSet() {
        std::lock_guard<std::mutex> lock(m_descriptorUpdateMutex);
        return m_set; 
    }
    
    VkDescriptorSetLayout getLayout() const { return m_layout; }

    // Update descriptor set with new data
    void updateDescriptorSet(const DescriptorSetBindings& bindings);

private:
    void createDescriptorPool();
    void allocateDescriptorSet();
    void writeDescriptorSet(const DescriptorSetBindings& bindings);

    void updateUsedCounts(const DescriptorSetBindings& bindings);
    
    static void destroyDescriptorPool();

    VkDevice m_device;
    VkDescriptorSetLayout m_layout;
    VkDescriptorSet m_set;
    
    // Track what this descriptor set is using for cleanup
    uint32_t m_usedBuffers = 0;
    uint32_t m_usedTextures = 0;
    uint32_t m_usedStorageBuffers = 0;
    uint32_t m_usedStorageImages = 0;
    uint32_t m_usedInputAttachments = 0;
    uint32_t m_usedAccelerationStructures = 0;
    std::mutex m_descriptorUpdateMutex;
    
    // Static pool management
    static VkDescriptorPool s_pool;
    static uint32_t s_poolRefCount;
    static uint32_t s_poolBufferCount;
    static uint32_t s_poolTextureCount;
    static uint32_t s_poolStorageBufferCount;
    static uint32_t s_poolStorageImageCount;
    static uint32_t s_poolInputAttachmentCount;
    static uint32_t s_poolAccelerationStructureCount;
    static const uint32_t s_maxSets = 1000;  // Maximum descriptor sets in pool
    static const uint32_t s_maxBuffers = 2000;
    static const uint32_t s_maxTextures = 4000;
    static const uint32_t s_maxStorageBuffers = 2000;
    static const uint32_t s_maxStorageImages = 2000;
    static const uint32_t s_maxInputAttachments = 1000;
    static const uint32_t s_maxAccelerationStructures = 64;
};

}



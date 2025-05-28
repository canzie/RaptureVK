#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <variant>
#include <memory>
#include <string>

#include <mutex>

namespace Rapture {

// Forward declarations
class UniformBuffer;
class Texture;

struct DescriptorSetBinding {
    uint32_t binding;
    VkDescriptorType type;
    uint32_t count;
    
    // Use variant to hold different resource types
    std::variant<std::shared_ptr<UniformBuffer>, std::shared_ptr<Texture>> resource;
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
    uint32_t m_usedInputAttachments = 0;
    std::mutex m_descriptorUpdateMutex;
    
    // Static pool management
    static VkDescriptorPool s_pool;
    static uint32_t s_poolRefCount;
    static uint32_t s_poolBufferCount;
    static uint32_t s_poolTextureCount;
    static uint32_t s_poolStorageBufferCount;
    static uint32_t s_poolInputAttachmentCount;
    static const uint32_t s_maxSets = 1000;  // Maximum descriptor sets in pool
    static const uint32_t s_maxBuffers = 2000;
    static const uint32_t s_maxTextures = 4000;
    static const uint32_t s_maxStorageBuffers = 2000;
    static const uint32_t s_maxInputAttachments = 1000;
};

}



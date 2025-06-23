#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <variant>
#include <memory>
#include <string>
#include <functional>
#include <mutex>

#include "Textures/TextureCommon.h"
#include "Buffers/Descriptors/DescriptorBinding.h"
#include "Pipelines/Pipeline.h"

namespace Rapture {

    // TODO create a caching system for descriptor sets
    // because right now we need the shader to give us the layout, which means each instance of a shader needs to create a new descriptor set
    // for a possible equal layout.
    // e.g. the gbuffer pass cant create the set because it does not have the layout, so the users need to create a set individually, leading to possible copies
    // we can fix this by using a cache system, this way we support both identical and slightly different ones.
    // we can go even further and log a warn when a layout can be optimised to be identical to a cached one.

// Forward declarations
class UniformBuffer;
class Buffer;
class Texture;
class TLAS;

// XYZ -> SET=X BIND=YZ
// 000 -> SET=0 BIND=00
// 101 -> SET=1 BIND=01
// 199 -> SET=1 BIND=99

enum class DescriptorSetBindingLocation {
    NONE,
    CAMERA_UBO = 0,
    LIGHTS_UBO = 1,
    SHADOW_MATRICES_UBO = 2,
    CASCADE_MATRICES_UBO = 3,
    SHADOW_DATA_UBO = 4,
    MATERIAL_UBO = 100,
    MESH_DATA_UBO = 200,
    BINDLESS_TEXTURES = 300,
    BINDLESS_SSBOS = 301,
};

inline uint32_t getBindingSetNumber(DescriptorSetBindingLocation location) {
    return static_cast<uint32_t>(location) / 100;
}

inline uint32_t getBindingBindNumber(DescriptorSetBindingLocation location) {
    return static_cast<uint32_t>(location) % 100;
}

struct DescriptorSetBinding {
    VkDescriptorType type;
    uint32_t count = 1;
    TextureViewType viewType = TextureViewType::DEFAULT;
    // Use variant to hold different resource types
    bool useStorageImageInfo = false; // Flag to use storage image descriptor info
    DescriptorSetBindingLocation location = DescriptorSetBindingLocation::NONE;

};

struct DescriptorSetBindings {
    std::vector<DescriptorSetBinding> bindings;
    uint32_t setNumber;
};


// TODO Find a way to only use the DescriptorBinding instead of the seperate ones
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

    // Typed getBinding methods for compile-time type safety
    std::shared_ptr<DescriptorBindingUniformBuffer> getUniformBufferBinding(DescriptorSetBindingLocation location) {
        auto it = m_uniformBufferBindings.find(location);
        return it != m_uniformBufferBindings.end() ? it->second : nullptr;
    }
    
    std::shared_ptr<DescriptorBindingTexture> getTextureBinding(DescriptorSetBindingLocation location) {
        auto it = m_textureBindings.find(location);
        return it != m_textureBindings.end() ? it->second : nullptr;
    }
    
    std::shared_ptr<DescriptorBindingTLAS> getTLASBinding(DescriptorSetBindingLocation location) {
        auto it = m_tlasBindings.find(location);
        return it != m_tlasBindings.end() ? it->second : nullptr;
    }

    std::shared_ptr<DescriptorBindingSSBO> getSSBOBinding(DescriptorSetBindingLocation location) {
        auto it = m_ssboBindings.find(location);
        return it != m_ssboBindings.end() ? it->second : nullptr;
    }

    void bind(VkCommandBuffer commandBuffer, std::shared_ptr<PipelineBase> pipeline);

private:
    void createBinding(const DescriptorSetBinding& binding);
    void createDescriptorSetLayout(const DescriptorSetBindings& bindings);
    void createDescriptorSet();


    void createDescriptorPool();

    void updateUsedCounts(const DescriptorSetBindings& bindings);
    
    static void destroyDescriptorPool();

    VkDevice m_device;
    VkDescriptorSetLayout m_layout;
    VkDescriptorSet m_set;

    std::unordered_map<DescriptorSetBindingLocation, std::shared_ptr<DescriptorBindingUniformBuffer>> m_uniformBufferBindings;
    std::unordered_map<DescriptorSetBindingLocation, std::shared_ptr<DescriptorBindingTexture>> m_textureBindings;
    std::unordered_map<DescriptorSetBindingLocation, std::shared_ptr<DescriptorBindingTLAS>> m_tlasBindings;
    std::unordered_map<DescriptorSetBindingLocation, std::shared_ptr<DescriptorBindingSSBO>> m_ssboBindings;

    uint32_t m_setNumber;
    
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

class DescriptorManager {
public:
    static void init();
    static void shutdown();

    static std::shared_ptr<DescriptorSet> getDescriptorSet(uint32_t setNumber);
    static std::shared_ptr<DescriptorSet> getDescriptorSet(DescriptorSetBindingLocation location);


private:
    static std::array<std::shared_ptr<DescriptorSet>, 4> s_descriptorSets;
    static std::array<std::mutex, 4> s_descriptorSetMutexes;

};

}



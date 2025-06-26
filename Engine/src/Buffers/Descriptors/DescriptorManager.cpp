#include "DescriptorManager.h"
#include "Logging/Log.h"
#include "WindowContext/Application.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Shaders/ShaderCommon.h"

namespace Rapture {

// Static member definitions
std::array<std::shared_ptr<DescriptorSet>, 4> DescriptorManager::s_descriptorSets;
std::array<std::mutex, 4> DescriptorManager::s_descriptorSetMutexes;

void DescriptorManager::init() {
    RP_CORE_INFO("Initializing DescriptorManager");
    
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    VkDevice device = vulkanContext.getLogicalDevice();
    
    // Initialize all descriptor sets based on DescriptorSetBindingLocation
    initializeSet0(); // Common resources (camera, lights, shadows)
    initializeSet1(); // Material resources 
    initializeSet2(); // Object/Mesh resources
    initializeSet3(); // Bindless resources (handled by DescriptorArrayManager)
}

void DescriptorManager::shutdown() {
    RP_CORE_INFO("Shutting down DescriptorManager");
    for (auto& set : s_descriptorSets) {
        set.reset();
    }
}

std::shared_ptr<DescriptorSet> DescriptorManager::getDescriptorSet(uint32_t setNumber) {
    if (setNumber >= s_descriptorSets.size()) {
        RP_CORE_ERROR("DescriptorManager::getDescriptorSet - set number {} out of bounds", setNumber);
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(s_descriptorSetMutexes[setNumber]);
    return s_descriptorSets[setNumber];
}

std::shared_ptr<DescriptorSet> DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation location) {
    uint32_t setNumber = getBindingSetNumber(location);
    return getDescriptorSet(setNumber);
}

void DescriptorManager::initializeSet0() {
    // Set 0: Common resources (camera, lights, shadows)
    DescriptorSetBindings bindings;
    
    // Add bindings for all common resources
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::CAMERA_UBO});
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::LIGHTS_UBO});
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 48, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::SHADOW_MATRICES_UBO});
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::CASCADE_MATRICES_UBO});
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::SHADOW_DATA_UBO});
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::PROBE_VOLUME_DATA_UBO});
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2048, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::MDI_INDEXED_INFO_SSBOS});

            
    bindings.setNumber = 0;

    
    // Create the descriptor set
    std::lock_guard<std::mutex> lock(s_descriptorSetMutexes[bindings.setNumber]);
    s_descriptorSets[bindings.setNumber] = std::make_shared<DescriptorSet>(bindings);

    RP_CORE_INFO("DescriptorManager Initialized set {}", bindings.setNumber);
}

void DescriptorManager::initializeSet1() {
    // Set 1: Material resources
    DescriptorSetBindings bindings;
    
    // Add bindings for material resources
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::MATERIAL_UBO});
    bindings.setNumber = 1;
    
    // Create the descriptor set
    std::lock_guard<std::mutex> lock(s_descriptorSetMutexes[bindings.setNumber]);
    s_descriptorSets[bindings.setNumber] = std::make_shared<DescriptorSet>(bindings);

    RP_CORE_INFO("DescriptorManager Initialized set {}", bindings.setNumber);
}

void DescriptorManager::initializeSet2() {
    // Set 2: Object/Mesh resources  
    DescriptorSetBindings bindings;
    
    // Add bindings for mesh data (using SSBO for bindless access)
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2048, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::MESH_DATA_UBO});
    bindings.setNumber = 2;
    
    // Create the descriptor set
    std::lock_guard<std::mutex> lock(s_descriptorSetMutexes[bindings.setNumber]);
    s_descriptorSets[bindings.setNumber] = std::make_shared<DescriptorSet>(bindings);

    RP_CORE_INFO("DescriptorManager Initialized set {}", bindings.setNumber);
}

void DescriptorManager::initializeSet3() {
    // Set 3: Bindless resources  
    DescriptorSetBindings bindings;
    
    //  Bindless textures
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::BINDLESS_TEXTURES});

    //  Bindless SSBOs
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2048, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::BINDLESS_SSBOS});

    // DDGI ray data storage
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, 
                                true, DescriptorSetBindingLocation::DDGI_RAY_DATA_STORAGE});
    
    // DDGI irradiance storage (current frame)
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, 
                                true, DescriptorSetBindingLocation::DDGI_IRRADIANCE_STORAGE});
    
    // DDGI irradiance storage (previous frame)
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, 
                                true, DescriptorSetBindingLocation::DDGI_PREV_IRRADIANCE_STORAGE});
    
    // DDGI visibility storage (current frame)
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, 
                                true, DescriptorSetBindingLocation::DDGI_VISIBILITY_STORAGE});
    
    // DDGI visibility storage (previous frame)
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, 
                                true, DescriptorSetBindingLocation::DDGI_PREV_VISIBILITY_STORAGE});
    
    // Flatten output storage (color textures)
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, 
                                true, DescriptorSetBindingLocation::FLATTEN_OUTPUT_STORAGE});

    // DDGI scene info SSBO
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::DDGI_SCENE_INFO_SSBOS});

    // Flatten depth output storage (depth textures)
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEPTH, 
                                true, DescriptorSetBindingLocation::FLATTEN_DEPTH_OUTPUT_STORAGE});

    // General purpose bindless storage textures
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 8, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::BINDLESS_ACCELERATION_STRUCTURES});

    bindings.setNumber = 3;
    
    // Create the descriptor set
    std::lock_guard<std::mutex> lock(s_descriptorSetMutexes[bindings.setNumber]);
    s_descriptorSets[bindings.setNumber] = std::make_shared<DescriptorSet>(bindings);

    RP_CORE_INFO("DescriptorManager Initialized set {}", bindings.setNumber);
}

void DescriptorManager::bindSet(uint32_t setNumber, std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<PipelineBase> pipeline) {

    if (setNumber >= s_descriptorSets.size()) {
        RP_CORE_ERROR("DescriptorManager::bindSet - set number {} out of bounds", setNumber);
        return;
    }

    s_descriptorSets[setNumber]->bind(commandBuffer->getCommandBufferVk(), pipeline);

}

void DescriptorManager::bindSet(DescriptorSetBindingLocation location, std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<PipelineBase> pipeline) {
    uint32_t setNumber = getBindingSetNumber(location);
    bindSet(setNumber, commandBuffer, pipeline);
}

std::vector<VkDescriptorSetLayout> DescriptorManager::getDescriptorSetLayouts() {
    std::vector<VkDescriptorSetLayout> layouts;
    
    for (size_t i = 0; i < s_descriptorSets.size(); ++i) {
        if (s_descriptorSets[i]) {
            layouts.push_back(s_descriptorSets[i]->getLayout());
        }
    }
    
    return layouts;
}

std::vector<VkDescriptorSetLayout> DescriptorManager::getDescriptorSetLayouts(const std::vector<uint32_t>& setNumbers) {
    std::vector<VkDescriptorSetLayout> layouts;
    
    for (uint32_t setNumber : setNumbers) {
        if (setNumber < s_descriptorSets.size() && s_descriptorSets[setNumber]) {
            layouts.push_back(s_descriptorSets[setNumber]->getLayout());
        } else {
            RP_CORE_WARN("DescriptorManager::getDescriptorSetLayouts - set number {} not available", setNumber);
        }
    }
    
    return layouts;
}

} // namespace Rapture 
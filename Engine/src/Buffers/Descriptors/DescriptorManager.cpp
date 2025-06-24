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
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::LIGHTS_UBO});
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::SHADOW_MATRICES_UBO});
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::CASCADE_MATRICES_UBO});
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::SHADOW_DATA_UBO});
    bindings.setNumber = 0;

    
    // Create the descriptor set
    std::lock_guard<std::mutex> lock(s_descriptorSetMutexes[0]);
    s_descriptorSets[0] = std::make_shared<DescriptorSet>(bindings);
}

void DescriptorManager::initializeSet1() {
    // Set 1: Material resources
    DescriptorSetBindings bindings;
    
    // Add bindings for material resources
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::MATERIAL_UBO});
    bindings.setNumber = 1;
    
    // Create the descriptor set
    std::lock_guard<std::mutex> lock(s_descriptorSetMutexes[1]);
    s_descriptorSets[1] = std::make_shared<DescriptorSet>(bindings);
}

void DescriptorManager::initializeSet2() {
    // Set 2: Object/Mesh resources  
    DescriptorSetBindings bindings;
    
    // Add bindings for mesh data (using SSBO for bindless access)
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::MESH_DATA_UBO});
    bindings.setNumber = 2;
    
    // Create the descriptor set
    std::lock_guard<std::mutex> lock(s_descriptorSetMutexes[2]);
    s_descriptorSets[2] = std::make_shared<DescriptorSet>(bindings);
}

void DescriptorManager::initializeSet3() {
    // Set 2: Object/Mesh resources  
    DescriptorSetBindings bindings;
    
    // Add bindings for bindless textures
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4096, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::BINDLESS_TEXTURES});

    // Add bindings for bindless SSBOs
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2048, TextureViewType::DEFAULT, 
                                false, DescriptorSetBindingLocation::BINDLESS_SSBOS});

    // Add bindings for bindless textures and storage images
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024, TextureViewType::DEFAULT, 
                                true, DescriptorSetBindingLocation::BINDLESS_STORAGE_TEXTURES});


    bindings.setNumber = 3;
    
    
    // Create the descriptor set
    std::lock_guard<std::mutex> lock(s_descriptorSetMutexes[2]);
    s_descriptorSets[2] = std::make_shared<DescriptorSet>(bindings);
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

} // namespace Rapture 
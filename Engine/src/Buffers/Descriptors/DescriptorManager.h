#pragma once

#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/Descriptors/DescriptorBinding.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"


namespace Rapture {


class DescriptorManager {
public:
    static void init();
    static void shutdown();

    static std::shared_ptr<DescriptorSet> getDescriptorSet(uint32_t setNumber);
    static std::shared_ptr<DescriptorSet> getDescriptorSet(DescriptorSetBindingLocation location);
    
    // Convenience method to bind resources to descriptor sets
    static void bindSet(uint32_t setNumber, std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<PipelineBase> pipeline);
    static void bindSet(DescriptorSetBindingLocation location, std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<PipelineBase> pipeline);

    // Get descriptor set layouts for pipeline creation
    static std::vector<VkDescriptorSetLayout> getDescriptorSetLayouts();
    static std::vector<VkDescriptorSetLayout> getDescriptorSetLayouts(const std::vector<uint32_t>& setNumbers);
private:
    static void initializeSet0(); // Common resources (camera, lights, shadows)
    static void initializeSet1(); // Material resources
    static void initializeSet2(); // Object/Mesh resources  
    static void initializeSet3(); // Bindless resources

    static std::array<std::shared_ptr<DescriptorSet>, 4> s_descriptorSets;
    static std::array<std::mutex, 4> s_descriptorSetMutexes;
};

}
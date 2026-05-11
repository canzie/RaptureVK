#ifndef RAPTURE__DESCRIPTOR_MANAGER_H
#define RAPTURE__DESCRIPTOR_MANAGER_H

#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/descriptors/DescriptorBinding.h"
#include "buffers/descriptors/DescriptorSet.h"

namespace Rapture {

class DescriptorManager {
  public:
    DescriptorManager();
    ~DescriptorManager();

    DescriptorManager(const DescriptorManager&) = delete;
    DescriptorManager& operator=(const DescriptorManager&) = delete;

    std::shared_ptr<DescriptorSet> getDescriptorSet(uint32_t setNumber);
    std::shared_ptr<DescriptorSet> getDescriptorSet(DescriptorSetBindingLocation location);

    // Convenience method to bind resources to descriptor sets
    void bindSet(uint32_t setNumber, CommandBuffer *commandBuffer, std::shared_ptr<PipelineBase> pipeline);
    void bindSet(DescriptorSetBindingLocation location, CommandBuffer *commandBuffer,
                 std::shared_ptr<PipelineBase> pipeline);

    // Get descriptor set layouts for pipeline creation
    std::vector<VkDescriptorSetLayout> getDescriptorSetLayouts();
    std::vector<VkDescriptorSetLayout> getDescriptorSetLayouts(const std::vector<uint32_t> &setNumbers);

  private:
    void initializeSet0(); // Common resources (camera, lights, shadows)
    void initializeSet1(); // Material resources
    void initializeSet2(); // Object/Mesh resources
    void initializeSet3(); // Bindless resources

    std::array<std::shared_ptr<DescriptorSet>, 4> m_descriptorSets;
    std::array<std::mutex, 4> m_descriptorSetMutexes;
};

} // namespace Rapture

#endif // RAPTURE__DESCRIPTOR_MANAGER_H
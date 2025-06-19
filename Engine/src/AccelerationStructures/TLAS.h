#pragma once

#include "BLAS.h"
#include "Buffers/Buffers.h"

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>


namespace Rapture {

struct TLASInstance {
    std::shared_ptr<BLAS> blas;
    glm::mat4 transform = glm::mat4(1.0f);
    uint32_t instanceCustomIndex = 0;
    uint32_t mask = 0xFF;
    uint32_t shaderBindingTableRecordOffset = 0;
    VkGeometryInstanceFlagsKHR flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    uint32_t entityID = 0;
};

class TLAS {
public:
    TLAS();
    ~TLAS();

    // Add a BLAS instance to the TLAS
    void addInstance(const TLASInstance& instance);
    
    // Build the acceleration structure
    void build();
    
    // Update the acceleration structure (useful for dynamic scenes)
    void update();
    
    // Update specific instances efficiently without full rebuild
    void updateInstances(const std::vector<std::pair<uint32_t, glm::mat4>>& instanceUpdates);
    
    // Update a single instance
    void updateInstance(uint32_t instanceIndex, const glm::mat4& newTransform);

    // Get the acceleration structure handle
    VkAccelerationStructureKHR getAccelerationStructure() const { return m_accelerationStructure; }
    
    // Get the device address of the acceleration structure
    VkDeviceAddress getDeviceAddress() const { return m_deviceAddress; }

    // Check if the acceleration structure has been built
    bool isBuilt() const { return m_isBuilt; }
    
    // Clear all instances
    void clear();
    
    // Get number of instances
    uint32_t getInstanceCount() const { return static_cast<uint32_t>(m_instances.size()); }
    const std::vector<TLASInstance>& getInstances() const { return m_instances; }
    std::vector<TLASInstance>& getInstances() { return m_instances; }

private:
    void createAccelerationStructure();
    void createInstanceBuffer();
    void buildAccelerationStructure();
    void updateInstanceBuffer(const std::vector<std::pair<uint32_t, glm::mat4>>& instanceUpdates);

private:
    std::vector<TLASInstance> m_instances;
    
    VkAccelerationStructureKHR m_accelerationStructure;
    VkAccelerationStructureGeometryKHR m_geometry;
    VkAccelerationStructureBuildGeometryInfoKHR m_buildInfo;
    VkAccelerationStructureBuildRangeInfoKHR m_buildRangeInfo;
    
    // Buffer to hold the acceleration structure
    VkBuffer m_buffer;
    VmaAllocation m_allocation;
    
    // Buffer for instances data
    VkBuffer m_instanceBuffer;
    VmaAllocation m_instanceAllocation;
    
    // Scratch buffer for building
    VkBuffer m_scratchBuffer;
    VmaAllocation m_scratchAllocation;
    
    VkDeviceAddress m_deviceAddress;
    VkDeviceSize m_accelerationStructureSize;
    VkDeviceSize m_scratchSize;
    
    bool m_isBuilt;
    bool m_needsRebuild;
    bool m_supportsUpdate;  // Whether the device supports AS updates
    
    // Vulkan handles
    VkDevice m_device;
    VmaAllocator m_allocator;
};

}

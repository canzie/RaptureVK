#pragma once

#include "Meshes/Mesh.h"
#include "Buffers/Buffers.h"

#include <memory>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace Rapture {

class BLAS {
public:
    BLAS(std::shared_ptr<Mesh> mesh);
    ~BLAS();

    // Build the acceleration structure
    void build();

    // Get the acceleration structure handle
    VkAccelerationStructureKHR getAccelerationStructure() const { return m_accelerationStructure; }
    
    // Get the device address of the acceleration structure
    VkDeviceAddress getDeviceAddress() const { return m_deviceAddress; }

    // Check if the acceleration structure has been built
    bool isBuilt() const { return m_isBuilt; }

private:
    void createAccelerationStructure();
    void createGeometry();

private:
    std::shared_ptr<Mesh> m_mesh;
    
    VkAccelerationStructureKHR m_accelerationStructure;
    VkAccelerationStructureGeometryKHR m_geometry;
    VkAccelerationStructureBuildGeometryInfoKHR m_buildInfo;
    VkAccelerationStructureBuildRangeInfoKHR m_buildRangeInfo;
    
    // Buffer to hold the acceleration structure
    VkBuffer m_buffer;
    VmaAllocation m_allocation;
    
    // Scratch buffer for building
    VkBuffer m_scratchBuffer;
    VmaAllocation m_scratchAllocation;
    
    VkDeviceAddress m_deviceAddress;
    VkDeviceSize m_accelerationStructureSize;
    VkDeviceSize m_scratchSize;
    
    bool m_isBuilt;
    
    // Vulkan handles
    VkDevice m_device;
    VmaAllocator m_allocator;
};

}
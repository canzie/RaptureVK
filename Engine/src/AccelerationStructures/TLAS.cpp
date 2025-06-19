#include "TLAS.h"
#include "WindowContext/Application.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Logging/Log.h"

#include <stdexcept>
#include <cstring>

namespace Rapture {

TLAS::TLAS()
    : m_accelerationStructure(VK_NULL_HANDLE)
    , m_buffer(VK_NULL_HANDLE)
    , m_allocation(VK_NULL_HANDLE)
    , m_instanceBuffer(VK_NULL_HANDLE)
    , m_instanceAllocation(VK_NULL_HANDLE)
    , m_scratchBuffer(VK_NULL_HANDLE)
    , m_scratchAllocation(VK_NULL_HANDLE)
    , m_deviceAddress(0)
    , m_accelerationStructureSize(0)
    , m_scratchSize(0)
    , m_isBuilt(false)
    , m_needsRebuild(false)
    , m_supportsUpdate(true)  // Assume true for now, could check device features
{
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    
    if (!vulkanContext.isRayTracingEnabled()) {
        RP_CORE_ERROR("TLAS: Ray tracing is not enabled on this device!");
        throw std::runtime_error("TLAS: Ray tracing is not enabled on this device!");
    }
    
    m_device = vulkanContext.getLogicalDevice();
    m_allocator = vulkanContext.getVmaAllocator();
}

TLAS::~TLAS() {
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    
    if (m_accelerationStructure != VK_NULL_HANDLE) {
        vulkanContext.vkDestroyAccelerationStructureKHR(m_device, m_accelerationStructure, nullptr);
    }
    
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    }
    
    if (m_instanceBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_instanceBuffer, m_instanceAllocation);
    }
    
    if (m_scratchBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_scratchBuffer, m_scratchAllocation);
    }
}

void TLAS::addInstance(const TLASInstance& instance) {
    if (!instance.blas) {
        RP_CORE_ERROR("TLAS: Cannot add instance with null BLAS!");
        return;
    }
    
    if (!instance.blas->isBuilt()) {
        RP_CORE_ERROR("TLAS: Cannot add instance with unbuilt BLAS!");
        return;
    }
    
    m_instances.push_back(instance);
    m_needsRebuild = true;
    
    if (m_isBuilt) {
        RP_CORE_INFO("TLAS: Instance added, rebuild required");
    }
}

void TLAS::clear() {
    m_instances.clear();
    m_needsRebuild = true;
    m_isBuilt = false;
}

void TLAS::createInstanceBuffer() {
    if (m_instances.empty()) {
        RP_CORE_ERROR("TLAS: Cannot create instance buffer with no instances!");
        return;
    }
    
    // Clean up old instance buffer if it exists
    if (m_instanceBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_instanceBuffer, m_instanceAllocation);
        m_instanceBuffer = VK_NULL_HANDLE;
        m_instanceAllocation = VK_NULL_HANDLE;
    }
    
    // Create buffer for instance data
    VkDeviceSize instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * m_instances.size();
    
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = instanceBufferSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | 
                           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    
    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    VmaAllocationInfo allocInfo{};
    if (vmaCreateBuffer(m_allocator, &bufferCreateInfo, &allocCreateInfo, 
                       &m_instanceBuffer, &m_instanceAllocation, &allocInfo) != VK_SUCCESS) {
        RP_CORE_ERROR("TLAS: Failed to create instance buffer!");
        throw std::runtime_error("TLAS: Failed to create instance buffer!");
    }
    
    // Fill instance data
    VkAccelerationStructureInstanceKHR* instanceData = 
        static_cast<VkAccelerationStructureInstanceKHR*>(allocInfo.pMappedData);
    
    for (size_t i = 0; i < m_instances.size(); ++i) {
        const auto& instance = m_instances[i];
        
        // Copy transform matrix (transposed for Vulkan)
        glm::mat4 transposed = glm::transpose(instance.transform);
        memcpy(&instanceData[i].transform, &transposed, sizeof(VkTransformMatrixKHR));
        
        instanceData[i].instanceCustomIndex = i;
        instanceData[i].mask = instance.mask;
        instanceData[i].instanceShaderBindingTableRecordOffset = instance.shaderBindingTableRecordOffset;
        instanceData[i].flags = instance.flags;
        instanceData[i].accelerationStructureReference = instance.blas->getDeviceAddress();
    }
    
    // Flush if memory is not coherent
    vmaFlushAllocation(m_allocator, m_instanceAllocation, 0, instanceBufferSize);
}

void TLAS::createAccelerationStructure()
{
    if (m_instances.empty()) {
        RP_CORE_ERROR("TLAS: Cannot create acceleration structure with no instances!");
        return;
    }
    
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    
    // Create instance buffer first
    createInstanceBuffer();
    
    // Get instance buffer device address
    VkBufferDeviceAddressInfo instanceAddressInfo{};
    instanceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instanceAddressInfo.buffer = m_instanceBuffer;
    VkDeviceAddress instanceAddress = vkGetBufferDeviceAddress(m_device, &instanceAddressInfo);
    
    // Set up geometry
    m_geometry = {};
    m_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    m_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    m_geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    m_geometry.geometry.instances.data.deviceAddress = instanceAddress;
    
    // Set up build info
    m_buildInfo = {};
    m_buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    m_buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    m_buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | 
                       VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    m_buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    m_buildInfo.geometryCount = 1;
    m_buildInfo.pGeometries = &m_geometry;
    
    // Set up build range info
    m_buildRangeInfo = {};
    m_buildRangeInfo.primitiveCount = static_cast<uint32_t>(m_instances.size());
    m_buildRangeInfo.primitiveOffset = 0;
    m_buildRangeInfo.firstVertex = 0;
    m_buildRangeInfo.transformOffset = 0;
    
    // Get size requirements
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    
    uint32_t primitiveCount = m_buildRangeInfo.primitiveCount;
    vulkanContext.vkGetAccelerationStructureBuildSizesKHR(
        m_device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &m_buildInfo,
        &primitiveCount,
        &sizeInfo
    );
    
    m_accelerationStructureSize = sizeInfo.accelerationStructureSize;
    m_scratchSize = sizeInfo.buildScratchSize;
    
    // Clean up old acceleration structure if it exists
    if (m_accelerationStructure != VK_NULL_HANDLE) {
        vulkanContext.vkDestroyAccelerationStructureKHR(m_device, m_accelerationStructure, nullptr);
        m_accelerationStructure = VK_NULL_HANDLE;
    }
    
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
    
    // Create buffer for acceleration structure
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = m_accelerationStructureSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | 
                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    
    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    if (vmaCreateBuffer(m_allocator, &bufferCreateInfo, &allocCreateInfo, &m_buffer, &m_allocation, nullptr) != VK_SUCCESS) {
        RP_CORE_ERROR("TLAS: Failed to create acceleration structure buffer!");
        throw std::runtime_error("TLAS: Failed to create acceleration structure buffer!");
    }
    
    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_buffer;
    createInfo.size = m_accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    
    if (vulkanContext.vkCreateAccelerationStructureKHR(m_device, &createInfo, nullptr, &m_accelerationStructure) != VK_SUCCESS) {
        RP_CORE_ERROR("TLAS: Failed to create acceleration structure!");
        throw std::runtime_error("TLAS: Failed to create acceleration structure!");
    }
    
    // Get device address
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = m_accelerationStructure;
    
    m_deviceAddress = vulkanContext.vkGetAccelerationStructureDeviceAddressKHR(m_device, &addressInfo);
    
    RP_CORE_INFO("TLAS: Acceleration structure created successfully");
}

void TLAS::build() {
    if (m_instances.empty()) {
        RP_CORE_ERROR("TLAS: Cannot build acceleration structure with no instances!");
        return;
    }
    
    // Create or recreate acceleration structure if needed
    createAccelerationStructure();
    
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    
    // Create scratch buffer
    VkBufferCreateInfo scratchBufferCreateInfo{};
    scratchBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    scratchBufferCreateInfo.size = m_scratchSize;
    scratchBufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    
    VmaAllocationCreateInfo scratchAllocCreateInfo{};
    scratchAllocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    // Clean up old scratch buffer
    if (m_scratchBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_scratchBuffer, m_scratchAllocation);
    }
    
    if (vmaCreateBuffer(m_allocator, &scratchBufferCreateInfo, &scratchAllocCreateInfo, 
                       &m_scratchBuffer, &m_scratchAllocation, nullptr) != VK_SUCCESS) {
        RP_CORE_ERROR("TLAS: Failed to create scratch buffer!");
        throw std::runtime_error("TLAS: Failed to create scratch buffer!");
    }
    
    // Get scratch buffer device address
    VkBufferDeviceAddressInfo scratchAddressInfo{};
    scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchAddressInfo.buffer = m_scratchBuffer;
    VkDeviceAddress scratchAddress = vkGetBufferDeviceAddress(m_device, &scratchAddressInfo);
    
    // Update build info with addresses
    m_buildInfo.dstAccelerationStructure = m_accelerationStructure;
    m_buildInfo.scratchData.deviceAddress = scratchAddress;
    
    // Create command buffer and build acceleration structure
    CommandPoolConfig poolConfig{};
    poolConfig.queueFamilyIndex = vulkanContext.getQueueFamilyIndices().graphicsFamily.value();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    
    auto commandPool = CommandPoolManager::createCommandPool(poolConfig);
    auto commandBuffer = commandPool->getCommandBuffer();
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer->getCommandBufferVk(), &beginInfo);
    
    // Build acceleration structure
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &m_buildRangeInfo;
    vulkanContext.vkCmdBuildAccelerationStructuresKHR(
        commandBuffer->getCommandBufferVk(),
        1,
        &m_buildInfo,
        &pBuildRangeInfo
    );
    
    // Add memory barrier
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    
    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        0,
        1, &barrier,
        0, nullptr,
        0, nullptr
    );
    
    commandBuffer->end();
    
    // Submit command buffer
    auto queue = vulkanContext.getGraphicsQueue();
    queue->submitQueue(commandBuffer, VK_NULL_HANDLE);
    queue->waitIdle();
    
    // Clean up scratch buffer immediately as it's no longer needed
    vmaDestroyBuffer(m_allocator, m_scratchBuffer, m_scratchAllocation);
    m_scratchBuffer = VK_NULL_HANDLE;
    m_scratchAllocation = VK_NULL_HANDLE;
    
    m_isBuilt = true;
    m_needsRebuild = false;
    RP_CORE_INFO("TLAS: Acceleration structure built successfully with {} instances", m_instances.size());
}

void TLAS::update() {
    if (!m_isBuilt) {
        RP_CORE_WARN("TLAS: Cannot update unbuilt acceleration structure, building instead");
        build();
        return;
    }
    
    if (!m_needsRebuild) {
        RP_CORE_INFO("TLAS: No update needed");
        return;
    }
    
    // For simplicity, we'll rebuild instead of update for now
    // In a more optimized implementation, you could use UPDATE mode
    build();
}

void TLAS::updateInstances(const std::vector<std::pair<uint32_t, glm::mat4>>& instanceUpdates) {
    if (!m_isBuilt) {
        RP_CORE_WARN("TLAS: Cannot update unbuilt acceleration structure");
        return;
    }
    
    if (instanceUpdates.empty()) {
        return;
    }
    
    // Update the internal instance data with new transforms
    for (const auto& [instanceIndex, newTransform] : instanceUpdates) {
        if (instanceIndex < m_instances.size()) {
            m_instances[instanceIndex].transform = newTransform;
        }
    }
    
    // Update instance buffer with changed instances
    updateInstanceBuffer(instanceUpdates);
    
    if (m_supportsUpdate) {
        // Use efficient update path
        auto& app = Application::getInstance();
        auto& vulkanContext = app.getVulkanContext();
        
        // Create scratch buffer for update
        VkBufferCreateInfo scratchBufferCreateInfo{};
        scratchBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        scratchBufferCreateInfo.size = m_scratchSize;
        scratchBufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        
        VmaAllocationCreateInfo scratchAllocCreateInfo{};
        scratchAllocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        
        VkBuffer updateScratchBuffer;
        VmaAllocation updateScratchAllocation;
        
        if (vmaCreateBuffer(m_allocator, &scratchBufferCreateInfo, &scratchAllocCreateInfo, 
                           &updateScratchBuffer, &updateScratchAllocation, nullptr) != VK_SUCCESS) {
            RP_CORE_ERROR("TLAS: Failed to create update scratch buffer!");
            // Fall back to full rebuild
            build();
            return;
        }
        
        // Get scratch buffer device address
        VkBufferDeviceAddressInfo scratchAddressInfo{};
        scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        scratchAddressInfo.buffer = updateScratchBuffer;
        VkDeviceAddress scratchAddress = vkGetBufferDeviceAddress(m_device, &scratchAddressInfo);
        
        // Update build info for update mode
        m_buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        m_buildInfo.srcAccelerationStructure = m_accelerationStructure;
        m_buildInfo.dstAccelerationStructure = m_accelerationStructure;
        m_buildInfo.scratchData.deviceAddress = scratchAddress;
        
        // Create command buffer for update
        CommandPoolConfig poolConfig{};
        poolConfig.queueFamilyIndex = vulkanContext.getQueueFamilyIndices().graphicsFamily.value();
        poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        
        auto commandPool = CommandPoolManager::createCommandPool(poolConfig);
        auto commandBuffer = commandPool->getCommandBuffer();
        
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        
        vkBeginCommandBuffer(commandBuffer->getCommandBufferVk(), &beginInfo);
        
        // Update acceleration structure
        const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &m_buildRangeInfo;
        vulkanContext.vkCmdBuildAccelerationStructuresKHR(
            commandBuffer->getCommandBufferVk(),
            1,
            &m_buildInfo,
            &pBuildRangeInfo
        );
        
        // Add memory barrier
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        
        vkCmdPipelineBarrier(
            commandBuffer->getCommandBufferVk(),
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0,
            1, &barrier,
            0, nullptr,
            0, nullptr
        );
        
        commandBuffer->end();
        
        // Submit command buffer
        auto queue = vulkanContext.getGraphicsQueue();
        queue->submitQueue(commandBuffer, VK_NULL_HANDLE);
        queue->waitIdle();
        
        // Clean up update scratch buffer
        vmaDestroyBuffer(m_allocator, updateScratchBuffer, updateScratchAllocation);
        
        // Reset build mode back to build
        m_buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        m_buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;
        
        RP_CORE_INFO("TLAS: Updated {} instances efficiently", instanceUpdates.size());
    } else {
        // Fall back to full rebuild if update is not supported
        RP_CORE_WARN("TLAS: Device doesn't support AS updates, falling back to rebuild");
        build();
    }
}

void TLAS::updateInstance(uint32_t instanceIndex, const glm::mat4& newTransform) {
    updateInstances({{instanceIndex, newTransform}});
}

void TLAS::updateInstanceBuffer(const std::vector<std::pair<uint32_t, glm::mat4>>& instanceUpdates) {
    if (m_instanceBuffer == VK_NULL_HANDLE || m_instances.empty()) {
        return;
    }
    
    if (instanceUpdates.empty()) {
        return; // No changes, nothing to update
    }
    
    // Get mapped data
    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(m_allocator, m_instanceAllocation, &allocInfo);
    
    if (!allocInfo.pMappedData) {
        RP_CORE_ERROR("TLAS: Instance buffer is not mapped!");
        return;
    }
    
    VkAccelerationStructureInstanceKHR* instanceData = 
        static_cast<VkAccelerationStructureInstanceKHR*>(allocInfo.pMappedData);
    
    // Update only changed instances
    for (const auto& [instanceIndex, newTransform] : instanceUpdates) {
        if (instanceIndex < m_instances.size()) {
            // Copy transform matrix (transposed for Vulkan)
            glm::mat4 transposed = glm::transpose(newTransform);
            memcpy(&instanceData[instanceIndex].transform, &transposed, sizeof(VkTransformMatrixKHR));
            
            instanceData[instanceIndex].instanceCustomIndex = instanceIndex;
            instanceData[instanceIndex].mask = m_instances[instanceIndex].mask;
            instanceData[instanceIndex].instanceShaderBindingTableRecordOffset = m_instances[instanceIndex].shaderBindingTableRecordOffset;
            instanceData[instanceIndex].flags = m_instances[instanceIndex].flags;
            instanceData[instanceIndex].accelerationStructureReference = m_instances[instanceIndex].blas->getDeviceAddress();
        }
    }
    
    // Flush specific ranges
    for (const auto& [instanceIndex, newTransform] : instanceUpdates) {
        if (instanceIndex < m_instances.size()) {
            VkDeviceSize offset = sizeof(VkAccelerationStructureInstanceKHR) * instanceIndex;
            VkDeviceSize size = sizeof(VkAccelerationStructureInstanceKHR);
            vmaFlushAllocation(m_allocator, m_instanceAllocation, offset, size);
        }
    }
}

} 
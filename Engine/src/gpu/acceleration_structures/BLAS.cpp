#include "BLAS.h"

#include "app/Application.h"
#include "assets/meshes/Mesh.h"
#include "core/utils/Log.h"
#include "core/utils/TracyProfiler.h"
#include "core/utils/rp_assert.h"
#include "gpu/command_buffers/CommandPool.h"

namespace Rapture {

BLAS::BLAS(const Mesh &mesh)
    : m_accelerationStructure(VK_NULL_HANDLE), m_buffer(VK_NULL_HANDLE), m_allocation(VK_NULL_HANDLE),
      m_scratchBuffer(VK_NULL_HANDLE), m_scratchAllocation(VK_NULL_HANDLE), m_deviceAddress(0), m_accelerationStructureSize(0),
      m_scratchSize(0), m_isBuilt(false), m_isValid(false), m_device(VK_NULL_HANDLE), m_allocator(VK_NULL_HANDLE)
{

    RAPTURE_PROFILE_FUNCTION();

    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();

    RP_ASSERT(vulkanContext.isRayTracingEnabled(), "BLAS created while ray tracing is not enabled on this device");
    if (!vulkanContext.isRayTracingEnabled()) {
        RP_CORE_ERROR("Ray tracing is not enabled on this device!");
        return;
    }

    m_device = vulkanContext.getLogicalDevice();
    m_allocator = vulkanContext.getVmaAllocator();

    if (!createGeometry(mesh)) {
        return;
    }

    m_isValid = createAccelerationStructure();
}

BLAS::~BLAS()
{
    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();

    if (m_accelerationStructure != VK_NULL_HANDLE) {
        vulkanContext.vkDestroyAccelerationStructureKHR(m_device, m_accelerationStructure, nullptr);
    }

    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    }

    if (m_scratchBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_scratchBuffer, m_scratchAllocation);
    }
}

bool BLAS::createGeometry(const Mesh &mesh)
{

    RAPTURE_PROFILE_FUNCTION();

    auto vertexAllocation = mesh.getVertexAllocation();
    auto indexAllocation = mesh.getIndexAllocation();

    if (vertexAllocation == nullptr || !vertexAllocation->isValid() || indexAllocation == nullptr || !indexAllocation->isValid()) {
        RP_CORE_ERROR("Mesh vertex or index buffer allocation is invalid");
        return false;
    }

    auto vertexBuffer = mesh.getVertexBuffer();
    auto indexBuffer = mesh.getIndexBuffer();

    if (vertexBuffer == nullptr || indexBuffer == nullptr) {
        RP_CORE_ERROR("Mesh vertex or index buffer is null");
        return false;
    }

    // Get vertex buffer device address from allocation
    VkBufferDeviceAddressInfo vertexBufferAddressInfo{};
    vertexBufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    vertexBufferAddressInfo.buffer = vertexAllocation->getBuffer();
    VkDeviceAddress vertexBufferBaseAddress = vkGetBufferDeviceAddress(m_device, &vertexBufferAddressInfo);
    VkDeviceAddress vertexAddress = vertexBufferBaseAddress + vertexAllocation->offsetBytes;

    // Get index buffer device address from allocation
    VkBufferDeviceAddressInfo indexBufferAddressInfo{};
    indexBufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    indexBufferAddressInfo.buffer = indexAllocation->getBuffer();
    VkDeviceAddress indexBufferBaseAddress = vkGetBufferDeviceAddress(m_device, &indexBufferAddressInfo);
    VkDeviceAddress indexAddress = indexBufferBaseAddress + indexAllocation->offsetBytes;

    // Set up geometry
    m_geometry = {};
    m_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    m_geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    m_geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;

    // Vertex data
    m_geometry.geometry.triangles.vertexData.deviceAddress = vertexAddress;

    // Get vertex stride from buffer layout, fallback to 12 bytes (3 floats for position)
    uint32_t vertexStride = vertexBuffer->getBufferLayout().calculateVertexSize();
    if (vertexStride == 0) {
        vertexStride = 12; // Default to 3 floats (position only)
        RP_CORE_WARN("BLAS: Buffer layout not set, assuming 12-byte stride (3 float position)");
    }

    m_geometry.geometry.triangles.vertexStride = vertexStride;
    m_geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT; // Position format
    m_geometry.geometry.triangles.maxVertex = static_cast<uint32_t>(vertexAllocation->sizeBytes / vertexStride) - 1;

    // Index data
    m_geometry.geometry.triangles.indexData.deviceAddress = indexAddress;

    // Get index type from index buffer
    VkIndexType indexType = indexBuffer->getIndexType();
    m_geometry.geometry.triangles.indexType = indexType;

    // Geometry flags
    m_geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    // Build range info
    m_buildRangeInfo = {};
    m_buildRangeInfo.primitiveCount = mesh.getIndexCount() / 3; // Number of triangles
    m_buildRangeInfo.primitiveOffset = 0;
    m_buildRangeInfo.firstVertex = 0;
    m_buildRangeInfo.transformOffset = 0;

    return true;
}

bool BLAS::createAccelerationStructure()
{
    RAPTURE_PROFILE_FUNCTION();

    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();

    // Set up build info
    m_buildInfo = {};
    m_buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    m_buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    m_buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    m_buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    m_buildInfo.geometryCount = 1;
    m_buildInfo.pGeometries = &m_geometry;

    // Get size requirements
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    uint32_t primitiveCount = m_buildRangeInfo.primitiveCount;
    vulkanContext.vkGetAccelerationStructureBuildSizesKHR(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &m_buildInfo,
                                                          &primitiveCount, &sizeInfo);

    m_accelerationStructureSize = sizeInfo.accelerationStructureSize;
    m_scratchSize = sizeInfo.buildScratchSize;

    // Create buffer for acceleration structure
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = m_accelerationStructureSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(m_allocator, &bufferCreateInfo, &allocCreateInfo, &m_buffer, &m_allocation, nullptr) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create acceleration structure buffer!");
        return false;
    }

    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_buffer;
    createInfo.size = m_accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    if (vulkanContext.vkCreateAccelerationStructureKHR(m_device, &createInfo, nullptr, &m_accelerationStructure) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create acceleration structure!");
        return false;
    }

    // Get device address
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = m_accelerationStructure;

    m_deviceAddress = vulkanContext.vkGetAccelerationStructureDeviceAddressKHR(m_device, &addressInfo);

    // RP_CORE_INFO("BLAS: Acceleration structure created successfully");
    return true;
}

void BLAS::build()
{
    RAPTURE_PROFILE_FUNCTION();

    if (m_isBuilt) {
        RP_CORE_WARN("Acceleration structure is already built");
        return;
    }

    RP_ASSERT(m_isValid, "BLAS::build called on a BLAS that failed construction");
    if (!m_isValid) {
        return;
    }

    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();

    // Get alignment for scratch buffer
    const auto &asProps = vulkanContext.getAccelerationStructureProperties();
    const VkDeviceSize scratchAlignment = asProps.minAccelerationStructureScratchOffsetAlignment;

    // Create scratch buffer
    VkBufferCreateInfo scratchBufferCreateInfo{};
    scratchBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    scratchBufferCreateInfo.size = m_scratchSize + scratchAlignment;
    scratchBufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo scratchAllocCreateInfo{};
    scratchAllocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(m_allocator, &scratchBufferCreateInfo, &scratchAllocCreateInfo, &m_scratchBuffer, &m_scratchAllocation,
                        nullptr) != VK_SUCCESS) {
        RP_CORE_ERROR("BLAS: Failed to create scratch buffer!");
        return;
    }

    // Get scratch buffer device address
    VkBufferDeviceAddressInfo scratchAddressInfo{};
    scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchAddressInfo.buffer = m_scratchBuffer;
    VkDeviceAddress scratchAddress = vkGetBufferDeviceAddress(m_device, &scratchAddressInfo);

    // Align the scratch address
    VkDeviceAddress alignedScratchAddress = (scratchAddress + scratchAlignment - 1) & ~(scratchAlignment - 1);

    // Update build info with addresses
    m_buildInfo.dstAccelerationStructure = m_accelerationStructure;
    m_buildInfo.scratchData.deviceAddress = alignedScratchAddress;

    // Create command buffer and build acceleration structure
    CommandPoolConfig poolConfig{};
    poolConfig.queueFamilyIndex = vulkanContext.getGraphicsQueueIndex();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolConfig.resetFlags = VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT;

    auto &rc = vulkanContext.getRenderContext();
    auto commandPoolHash = rc.commandPoolManager->createCommandPool(poolConfig);
    auto commandPool = rc.commandPoolManager->getCommandPool(commandPoolHash);
    auto commandBuffer = commandPool->getPrimaryCommandBuffer();

    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    // Build acceleration structure
    const VkAccelerationStructureBuildRangeInfoKHR *pBuildRangeInfo = &m_buildRangeInfo;
    vulkanContext.vkCmdBuildAccelerationStructuresKHR(commandBuffer->getCommandBufferVk(), 1, &m_buildInfo, &pBuildRangeInfo);

    // Add memory barrier
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    commandBuffer->end();

    // Submit command buffer
    auto queue = vulkanContext.getGraphicsQueue();
    queue->submitQueue(commandBuffer, nullptr, nullptr, VK_NULL_HANDLE);
    queue->waitIdle();

    // Clean up scratch buffer immediately as it's no longer needed
    vmaDestroyBuffer(m_allocator, m_scratchBuffer, m_scratchAllocation);
    m_scratchBuffer = VK_NULL_HANDLE;
    m_scratchAllocation = VK_NULL_HANDLE;

    m_isBuilt = true;
    // RP_CORE_INFO("BLAS: Acceleration structure built successfully");
}

} // namespace Rapture

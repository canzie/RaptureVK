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
      m_compactedAccelerationStructure(VK_NULL_HANDLE), m_compactedBuffer(VK_NULL_HANDLE),
      m_compactedAllocation(VK_NULL_HANDLE), m_deviceAddress(0), m_accelerationStructureSize(0), m_scratchSize(0),
      m_isBuilt(false), m_isReady(false), m_isValid(false), m_device(VK_NULL_HANDLE), m_allocator(VK_NULL_HANDLE)
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

    if (m_compactedAccelerationStructure != VK_NULL_HANDLE) {
        vulkanContext.vkDestroyAccelerationStructureKHR(m_device, m_compactedAccelerationStructure, nullptr);
    }

    if (m_compactedBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_compactedBuffer, m_compactedAllocation);
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

    // Get vertex stride from buffer layout, fallback to 12 bytes (3 floats for position)
    uint32_t vertexStride = vertexBuffer->getBufferLayout().calculateVertexSize();
    if (vertexStride == 0) {
        vertexStride = 12; // Default to 3 floats (position only)
        RP_CORE_WARN("BLAS: Buffer layout not set, assuming 12-byte stride (3 float position)");
    }

    VkIndexType indexType = indexBuffer->getIndexType();
    const uint32_t indexSize = indexType == VK_INDEX_TYPE_UINT32 ? 4 : 2;

    m_geometries.clear();
    m_buildRanges.clear();

    for (const MeshSection &section : mesh.getSections()) {
        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;

        geometry.geometry.triangles.vertexData.deviceAddress = vertexAddress;
        geometry.geometry.triangles.vertexStride = vertexStride;
        geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT; // Position format
        geometry.geometry.triangles.maxVertex = static_cast<uint32_t>(vertexAllocation->sizeBytes / vertexStride) - 1;

        geometry.geometry.triangles.indexData.deviceAddress = indexAddress;
        geometry.geometry.triangles.indexType = indexType;

        // TODO: an alpha masked run has to drop this so its cutouts are tested, which needs the
        // material's blend mode to reach here
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = section.indexCount / 3;
        range.primitiveOffset = section.firstIndex * indexSize;
        range.firstVertex = 0;
        range.transformOffset = 0;

        m_geometries.push_back(geometry);
        m_buildRanges.push_back(range);
    }

    if (m_geometries.empty()) {
        RP_CORE_ERROR("Mesh holds no runs to build an acceleration structure from");
        return false;
    }

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
    m_buildInfo.flags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    m_buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    m_buildInfo.geometryCount = static_cast<uint32_t>(m_geometries.size());
    m_buildInfo.pGeometries = m_geometries.data();

    // Get size requirements
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    std::vector<uint32_t> primitiveCounts;
    primitiveCounts.reserve(m_buildRanges.size());
    for (const VkAccelerationStructureBuildRangeInfoKHR &range : m_buildRanges) {
        primitiveCounts.push_back(range.primitiveCount);
    }

    vulkanContext.vkGetAccelerationStructureBuildSizesKHR(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &m_buildInfo,
                                                          primitiveCounts.data(), &sizeInfo);

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

void BLAS::recordBuild(VkCommandBuffer commandBuffer, VkDeviceAddress scratchAddress)
{
    RAPTURE_PROFILE_FUNCTION();

    RP_ASSERT(m_isValid, "BLAS::recordBuild called on a BLAS that failed construction");
    if (!m_isValid) {
        return;
    }

    auto &vulkanContext = Application::getInstance().getVulkanContext();

    m_buildInfo.dstAccelerationStructure = m_accelerationStructure;
    m_buildInfo.scratchData.deviceAddress = scratchAddress;

    const VkAccelerationStructureBuildRangeInfoKHR *pBuildRangeInfo = m_buildRanges.data();
    vulkanContext.vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &m_buildInfo, &pBuildRangeInfo);
}

bool BLAS::createCompacted(VkDeviceSize compactedSize)
{
    RAPTURE_PROFILE_FUNCTION();

    auto &vulkanContext = Application::getInstance().getVulkanContext();

    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = compactedSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(m_allocator, &bufferCreateInfo, &allocCreateInfo, &m_compactedBuffer, &m_compactedAllocation, nullptr) !=
        VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create the compacted acceleration structure buffer");
        return false;
    }

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_compactedBuffer;
    createInfo.size = compactedSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    if (vulkanContext.vkCreateAccelerationStructureKHR(m_device, &createInfo, nullptr, &m_compactedAccelerationStructure) !=
        VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create the compacted acceleration structure");
        vmaDestroyBuffer(m_allocator, m_compactedBuffer, m_compactedAllocation);
        m_compactedBuffer = VK_NULL_HANDLE;
        m_compactedAllocation = VK_NULL_HANDLE;
        return false;
    }

    m_accelerationStructureSize = compactedSize;
    return true;
}

void BLAS::recordCompactCopy(VkCommandBuffer commandBuffer)
{
    auto &vulkanContext = Application::getInstance().getVulkanContext();

    VkCopyAccelerationStructureInfoKHR copyInfo{};
    copyInfo.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
    copyInfo.src = m_accelerationStructure;
    copyInfo.dst = m_compactedAccelerationStructure;
    copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;

    vulkanContext.vkCmdCopyAccelerationStructureKHR(commandBuffer, &copyInfo);
}

void BLAS::adoptCompacted()
{
    if (m_compactedAccelerationStructure == VK_NULL_HANDLE) {
        return;
    }

    auto &vulkanContext = Application::getInstance().getVulkanContext();

    vulkanContext.vkDestroyAccelerationStructureKHR(m_device, m_accelerationStructure, nullptr);
    vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);

    m_accelerationStructure = m_compactedAccelerationStructure;
    m_buffer = m_compactedBuffer;
    m_allocation = m_compactedAllocation;

    m_compactedAccelerationStructure = VK_NULL_HANDLE;
    m_compactedBuffer = VK_NULL_HANDLE;
    m_compactedAllocation = VK_NULL_HANDLE;

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = m_accelerationStructure;

    m_deviceAddress = vulkanContext.vkGetAccelerationStructureDeviceAddressKHR(m_device, &addressInfo);
}

} // namespace Rapture

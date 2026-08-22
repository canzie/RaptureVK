#include "TLAS.h"

#include "app/Application.h"
#include "core/utils/Log.h"
#include "gpu/acceleration_structures/AccelerationStructureBuilder.h"
#include "core/utils/rp_assert.h"
#include "gpu/command_buffers/CommandPool.h"
#include "gpu/descriptors/DescriptorManager.h"

#include <algorithm>
#include <cstring>

namespace Rapture {

// VkAccelerationStructureInstanceKHR::instanceCustomIndex is 24 bits wide
static constexpr uint32_t MAX_TLAS_SLOTS = 1u << 24;

static VkDeviceAddress s_alignAddress(VkDeviceAddress address, VkDeviceSize alignment)
{
    return (address + alignment - 1) & ~(alignment - 1);
}

static VkDeviceAddress s_getBufferAddress(VkDevice device, VkBuffer buffer)
{
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = buffer;
    return vkGetBufferDeviceAddress(device, &addressInfo);
}

TLAS::TLAS()
    : m_slotCapacity(0), m_revision(0), m_accelerationStructure(VK_NULL_HANDLE), m_buffer(VK_NULL_HANDLE),
      m_allocation(VK_NULL_HANDLE), m_deviceAddress(0), m_accelerationStructureSize(0), m_buildScratchSize(0),
      m_updateScratchSize(0), m_isBuilt(false), m_needsRebuild(false), m_isWaitingOnStructures(false),
      m_device(VK_NULL_HANDLE), m_allocator(VK_NULL_HANDLE), m_bindlessIndex(UINT32_MAX)
{
    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();

    RP_ASSERT(vulkanContext.isRayTracingEnabled(), "TLAS created while ray tracing is not enabled on this device");
    if (!vulkanContext.isRayTracingEnabled()) {
        RP_CORE_ERROR("Ray tracing is not enabled on this device");
        return;
    }

    m_device = vulkanContext.getLogicalDevice();
    m_allocator = vulkanContext.getVmaAllocator();

    m_structuresReadyConnection = vulkanContext.getRenderContext().accelerationStructureBuilder->onStructuresReady.connect(
        [this]() { m_isWaitingOnStructures.store(false, std::memory_order_release); });

    m_buildResources.resize(std::max(1u, app.getFramesInFlight()));

    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    for (BuildResources &resources : m_buildResources) {
        if (vkCreateFence(m_device, &fenceCreateInfo, nullptr, &resources.fence) != VK_SUCCESS) {
            RP_CORE_ERROR("Failed to create an acceleration structure build fence");
        }
    }
}

TLAS::~TLAS()
{
    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();

    vulkanContext.waitIdle();

    destroyBuildResources();

    if (m_accelerationStructure != VK_NULL_HANDLE) {
        vulkanContext.vkDestroyAccelerationStructureKHR(m_device, m_accelerationStructure, nullptr);
    }

    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    }

    if (m_bindlessIndex != UINT32_MAX) {
        auto &rc = vulkanContext.getRenderContext();
        auto bindlessSet = rc.descriptorManager->getDescriptorSet(DescriptorSetBindingLocation::BINDLESS_ACCELERATION_STRUCTURES);
        if (bindlessSet) {
            auto binding = bindlessSet->getTLASBinding(DescriptorSetBindingLocation::BINDLESS_ACCELERATION_STRUCTURES);
            if (binding) {
                binding->free(m_bindlessIndex);
            }
        }
    }
}

void TLAS::destroyBuildResources()
{
    for (BuildResources &resources : m_buildResources) {
        if (resources.instanceBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, resources.instanceBuffer, resources.instanceAllocation);
        }

        if (resources.scratchBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, resources.scratchBuffer, resources.scratchAllocation);
        }

        if (resources.fence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, resources.fence, nullptr);
        }
    }

    m_buildResources.clear();
}

uint32_t TLAS::addInstance(const TLASInstance &instance)
{
    if (instance.blas == nullptr) {
        RP_CORE_ERROR("Cannot add an instance without a bottom level structure");
        return INVALID_TLAS_SLOT;
    }

    m_needsRebuild = true;
    m_revision++;

    // an entity that already traces keeps its slot, so anything indexed by it survives the swap
    auto it = m_entityToIndex.find(instance.entityId);
    if (it != m_entityToIndex.end()) {
        TLASInstance &existing = m_instances[it->second];
        uint32_t slot = existing.slot;
        existing = instance;
        existing.slot = slot;
        return slot;
    }

    uint32_t slot;
    if (m_freeSlots.empty()) {
        if (m_slotCapacity >= MAX_TLAS_SLOTS) {
            RP_CORE_ERROR("Ran out of instance slots, the scene has more than {} traced meshes", MAX_TLAS_SLOTS);
            return INVALID_TLAS_SLOT;
        }
        slot = m_slotCapacity++;
    } else {
        slot = m_freeSlots.back();
        m_freeSlots.pop_back();
    }

    TLASInstance stored = instance;
    stored.slot = slot;

    m_entityToIndex[stored.entityId] = static_cast<uint32_t>(m_instances.size());
    m_instances.push_back(stored);

    return slot;
}

void TLAS::removeInstance(uint32_t entityId)
{
    auto it = m_entityToIndex.find(entityId);
    if (it == m_entityToIndex.end()) {
        return;
    }

    uint32_t index = it->second;
    uint32_t lastIndex = static_cast<uint32_t>(m_instances.size()) - 1;

    m_freeSlots.push_back(m_instances[index].slot);

    if (index != lastIndex) {
        m_instances[index] = m_instances[lastIndex];
        m_entityToIndex[m_instances[index].entityId] = index;
    }

    m_instances.pop_back();
    m_entityToIndex.erase(entityId);

    // the pending writes name rows that the fill above just moved, and a rebuild rewrites them all anyway
    m_dirtyInstances.clear();

    m_needsRebuild = true;
    m_revision++;

    if (m_instances.empty()) {
        m_isBuilt = false;
    }
}

void TLAS::clear()
{
    m_instances.clear();
    m_entityToIndex.clear();
    m_dirtyInstances.clear();
    m_freeSlots.clear();

    m_slotCapacity = 0;
    m_needsRebuild = true;
    m_isBuilt = false;
    m_revision++;
}

bool TLAS::setInstanceTransform(uint32_t entityId, const glm::mat4 &transform)
{
    auto it = m_entityToIndex.find(entityId);
    if (it == m_entityToIndex.end()) {
        return false;
    }

    TLASInstance &instance = m_instances[it->second];
    if (instance.transform == transform) {
        return false;
    }

    instance.transform = transform;
    m_dirtyInstances.insert(it->second);
    return true;
}

TLAS::BuildResources &TLAS::acquireBuildResources()
{
    auto &app = Application::getInstance();

    BuildResources &resources = m_buildResources[app.getFrameInFlightIndex() % m_buildResources.size()];

    if (resources.isSubmitted && resources.fence != VK_NULL_HANDLE) {
        vkWaitForFences(m_device, 1, &resources.fence, VK_TRUE, UINT64_MAX);
        vkResetFences(m_device, 1, &resources.fence);
        resources.isSubmitted = false;
    }

    return resources;
}

VkDeviceAddress TLAS::writeInstanceBuffer(BuildResources &resources)
{
    VkDeviceSize requiredBytes = sizeof(VkAccelerationStructureInstanceKHR) * m_instances.size();

    if (resources.instanceCapacityBytes < requiredBytes) {
        if (resources.instanceBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, resources.instanceBuffer, resources.instanceAllocation);
            resources.instanceBuffer = VK_NULL_HANDLE;
            resources.instanceAllocation = VK_NULL_HANDLE;
            resources.instanceCapacityBytes = 0;
        }

        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = requiredBytes;
        bufferCreateInfo.usage =
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        if (vmaCreateBuffer(m_allocator, &bufferCreateInfo, &allocCreateInfo, &resources.instanceBuffer,
                            &resources.instanceAllocation, nullptr) != VK_SUCCESS) {
            RP_CORE_ERROR("Failed to create the instance buffer");
            return 0;
        }

        resources.instanceCapacityBytes = requiredBytes;
    }

    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(m_allocator, resources.instanceAllocation, &allocInfo);

    if (allocInfo.pMappedData == nullptr) {
        RP_CORE_ERROR("The instance buffer is not mapped");
        return 0;
    }

    auto *instanceData = static_cast<VkAccelerationStructureInstanceKHR *>(allocInfo.pMappedData);

    for (size_t i = 0; i < m_instances.size(); ++i) {
        const TLASInstance &instance = m_instances[i];

        glm::mat4 transposed = glm::transpose(instance.transform);
        memcpy(&instanceData[i].transform, &transposed, sizeof(VkTransformMatrixKHR));

        instanceData[i].instanceCustomIndex = instance.slot;
        instanceData[i].mask = instance.mask;
        instanceData[i].instanceShaderBindingTableRecordOffset = instance.shaderBindingTableRecordOffset;
        instanceData[i].flags = instance.flags;
        instanceData[i].accelerationStructureReference = instance.blas->getDeviceAddress();
    }

    vmaFlushAllocation(m_allocator, resources.instanceAllocation, 0, requiredBytes);

    return s_getBufferAddress(m_device, resources.instanceBuffer);
}

VkDeviceAddress TLAS::reserveScratch(BuildResources &resources, VkDeviceSize sizeBytes)
{
    auto &vulkanContext = Application::getInstance().getVulkanContext();

    const auto &asProps = vulkanContext.getAccelerationStructureProperties();
    const VkDeviceSize alignment = asProps.minAccelerationStructureScratchOffsetAlignment;
    const VkDeviceSize requiredBytes = sizeBytes + alignment;

    if (resources.scratchCapacityBytes < requiredBytes) {
        if (resources.scratchBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, resources.scratchBuffer, resources.scratchAllocation);
            resources.scratchBuffer = VK_NULL_HANDLE;
            resources.scratchAllocation = VK_NULL_HANDLE;
            resources.scratchCapacityBytes = 0;
        }

        VkBufferCreateInfo scratchBufferCreateInfo{};
        scratchBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        scratchBufferCreateInfo.size = requiredBytes;
        scratchBufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        VmaAllocationCreateInfo scratchAllocCreateInfo{};
        scratchAllocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateBuffer(m_allocator, &scratchBufferCreateInfo, &scratchAllocCreateInfo, &resources.scratchBuffer,
                            &resources.scratchAllocation, nullptr) != VK_SUCCESS) {
            RP_CORE_ERROR("Failed to create the scratch buffer");
            return 0;
        }

        resources.scratchCapacityBytes = requiredBytes;
    }

    return s_alignAddress(s_getBufferAddress(m_device, resources.scratchBuffer), alignment);
}

bool TLAS::createAccelerationStructure()
{
    auto &vulkanContext = Application::getInstance().getVulkanContext();

    if (m_accelerationStructure != VK_NULL_HANDLE) {
        vulkanContext.vkDestroyAccelerationStructureKHR(m_device, m_accelerationStructure, nullptr);
        m_accelerationStructure = VK_NULL_HANDLE;
    }

    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }

    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = m_accelerationStructureSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(m_allocator, &bufferCreateInfo, &allocCreateInfo, &m_buffer, &m_allocation, nullptr) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create the acceleration structure buffer");
        return false;
    }

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_buffer;
    createInfo.size = m_accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    if (vulkanContext.vkCreateAccelerationStructureKHR(m_device, &createInfo, nullptr, &m_accelerationStructure) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create the acceleration structure");
        return false;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = m_accelerationStructure;

    m_deviceAddress = vulkanContext.vkGetAccelerationStructureDeviceAddressKHR(m_device, &addressInfo);
    return true;
}

void TLAS::submitBuild(VkBuildAccelerationStructureModeKHR mode, BuildResources &resources, VkDeviceAddress scratchAddress)
{
    auto &vulkanContext = Application::getInstance().getVulkanContext();

    m_buildInfo.mode = mode;
    m_buildInfo.srcAccelerationStructure =
        (mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR) ? m_accelerationStructure : VK_NULL_HANDLE;
    m_buildInfo.dstAccelerationStructure = m_accelerationStructure;
    m_buildInfo.scratchData.deviceAddress = scratchAddress;

    CommandPoolConfig poolConfig{};
    poolConfig.queueFamilyIndex = vulkanContext.getGraphicsQueueIndex();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    auto &rc = vulkanContext.getRenderContext();
    auto commandPoolHash = rc.commandPoolManager->createCommandPool(poolConfig);
    auto commandPool = rc.commandPoolManager->getCommandPool(commandPoolHash);
    auto commandBuffer = commandPool->getPrimaryCommandBuffer();

    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    // a refit writes in place, so tracing submitted before it has to be finished reading the structure
    VkMemoryBarrier beforeBarrier{};
    beforeBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    beforeBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    beforeBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &beforeBarrier, 0, nullptr, 0, nullptr);

    const VkAccelerationStructureBuildRangeInfoKHR *pBuildRangeInfo = &m_buildRangeInfo;
    vulkanContext.vkCmdBuildAccelerationStructuresKHR(commandBuffer->getCommandBufferVk(), 1, &m_buildInfo, &pBuildRangeInfo);

    VkMemoryBarrier afterBarrier{};
    afterBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    afterBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    afterBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &afterBarrier, 0, nullptr, 0, nullptr);

    commandBuffer->end();

    vulkanContext.getGraphicsQueue()->submitQueue(commandBuffer, nullptr, nullptr, nullptr, resources.fence);
    resources.isSubmitted = true;
}

void TLAS::build()
{
    if (m_instances.empty()) {
        RP_CORE_ERROR("Cannot build a top level structure with no instances");
        return;
    }

    // every instance is traced against the moment this goes live, so none of them may be unbuilt
    for (const TLASInstance &instance : m_instances) {
        if (!instance.blas->isReady()) {
            m_isWaitingOnStructures.store(true, std::memory_order_release);
            return;
        }
    }

    auto &vulkanContext = Application::getInstance().getVulkanContext();

    // the structure below is replaced, so nothing in flight may still be tracing against it
    vulkanContext.waitIdle();

    BuildResources &resources = acquireBuildResources();

    VkDeviceAddress instanceAddress = writeInstanceBuffer(resources);
    if (instanceAddress == 0) {
        return;
    }

    m_geometry = {};
    m_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    m_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    m_geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    m_geometry.geometry.instances.data.deviceAddress = instanceAddress;

    m_buildInfo = {};
    m_buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    m_buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    m_buildInfo.flags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    m_buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    m_buildInfo.geometryCount = 1;
    m_buildInfo.pGeometries = &m_geometry;

    m_buildRangeInfo = {};
    m_buildRangeInfo.primitiveCount = static_cast<uint32_t>(m_instances.size());
    m_buildRangeInfo.primitiveOffset = 0;
    m_buildRangeInfo.firstVertex = 0;
    m_buildRangeInfo.transformOffset = 0;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    uint32_t primitiveCount = m_buildRangeInfo.primitiveCount;
    vulkanContext.vkGetAccelerationStructureBuildSizesKHR(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &m_buildInfo,
                                                          &primitiveCount, &sizeInfo);

    m_accelerationStructureSize = sizeInfo.accelerationStructureSize;
    m_buildScratchSize = sizeInfo.buildScratchSize;
    m_updateScratchSize = sizeInfo.updateScratchSize;

    if (!createAccelerationStructure()) {
        m_isBuilt = false;
        return;
    }

    VkDeviceAddress scratchAddress = reserveScratch(resources, m_buildScratchSize);
    if (scratchAddress == 0) {
        m_isBuilt = false;
        return;
    }

    submitBuild(VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, resources, scratchAddress);

    m_isBuilt = true;
    m_needsRebuild = false;
    m_dirtyInstances.clear();

    RP_CORE_INFO("Built the top level structure with {} instances", m_instances.size());

    registerWithDescriptorManager();
}

void TLAS::flushInstanceUpdates()
{
    if (m_dirtyInstances.empty()) {
        return;
    }

    // a rebuild rewrites every instance from the same records, so the pending writes ride along with it
    if (m_needsRebuild || !m_isBuilt) {
        return;
    }

    BuildResources &resources = acquireBuildResources();

    VkDeviceAddress instanceAddress = writeInstanceBuffer(resources);
    if (instanceAddress == 0) {
        return;
    }

    VkDeviceAddress scratchAddress = reserveScratch(resources, m_updateScratchSize);
    if (scratchAddress == 0) {
        return;
    }

    m_geometry.geometry.instances.data.deviceAddress = instanceAddress;

    submitBuild(VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR, resources, scratchAddress);

    m_dirtyInstances.clear();
}

void TLAS::registerWithDescriptorManager()
{
    auto &rc = Application::getInstance().getVulkanContext().getRenderContext();

    auto bindlessSet = rc.descriptorManager->getDescriptorSet(DescriptorSetBindingLocation::BINDLESS_ACCELERATION_STRUCTURES);
    if (!bindlessSet) {
        RP_CORE_ERROR("Failed to get the bindless acceleration structure descriptor set");
        return;
    }

    auto binding = bindlessSet->getTLASBinding(DescriptorSetBindingLocation::BINDLESS_ACCELERATION_STRUCTURES);
    if (!binding) {
        RP_CORE_ERROR("Failed to get the bindless acceleration structure binding");
        return;
    }

    if (m_bindlessIndex != UINT32_MAX) {
        binding->update(*this, m_bindlessIndex);
        return;
    }

    m_bindlessIndex = binding->add(*this);
}

} // namespace Rapture

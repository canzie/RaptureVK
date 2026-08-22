#include "AccelerationStructureBuilder.h"

#include "app/Application.h"
#include "assets/asset_manager/AssetManager.h"
#include "assets/meshes/Mesh.h"
#include "core/jobs/JobSystem.h"
#include "core/utils/Log.h"
#include "core/utils/TracyProfiler.h"
#include "gpu/acceleration_structures/BLAS.h"
#include "gpu/command_buffers/CommandPool.h"
#include "gpu/vulkan_context/TimelineSemaphore.h"

#include <thread>

namespace Rapture {

// scratch a single batch may take, so a large import is built over several passes rather than at once
static constexpr VkDeviceSize MAX_BATCH_SCRATCH_BYTES = 64ull * 1024ull * 1024ull;

static VkDeviceSize s_align(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

AccelerationStructureBuilder::AccelerationStructureBuilder()
{
    auto &vulkanContext = Application::getInstance().getVulkanContext();

    m_device = vulkanContext.getLogicalDevice();
    m_allocator = vulkanContext.getVmaAllocator();
}

AccelerationStructureBuilder::~AccelerationStructureBuilder()
{
    releaseBuildResources();
}

void AccelerationStructureBuilder::releaseBuildResources()
{
    if (m_scratchBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_scratchBuffer, m_scratchAllocation);
        m_scratchBuffer = VK_NULL_HANDLE;
        m_scratchAllocation = VK_NULL_HANDLE;
        m_scratchCapacityBytes = 0;
    }

    if (m_compactedSizeQueryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_device, m_compactedSizeQueryPool, nullptr);
        m_compactedSizeQueryPool = VK_NULL_HANDLE;
        m_queryPoolCapacity = 0;
    }
}

void AccelerationStructureBuilder::enqueue(Mesh &mesh, AssetHandle assetHandle)
{
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pending.push_back({&mesh, AssetManager::getAsset(assetHandle)});
    }

    kickDrain();
}

void AccelerationStructureBuilder::kickDrain()
{
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (m_isDraining || m_pending.empty()) {
            return;
        }
        m_isDraining = true;
    }

    jobs().run(JobDeclaration(
        [this](JobContext &ctx) {
            drainBatch(ctx);

            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                m_isDraining = false;
            }

            // anything enqueued while this job was finishing saw a drain running, so it needs its own
            kickDrain();
        },
        JobPriority::NORMAL, QueueAffinity::ANY, nullptr, "BLAS BUILD"));
}

std::vector<AccelerationStructureBuilder::PendingMesh> AccelerationStructureBuilder::takeBatch(VkDeviceSize &scratchBytes)
{
    auto &vulkanContext = Application::getInstance().getVulkanContext();
    const VkDeviceSize alignment = vulkanContext.getAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment;

    std::vector<PendingMesh> batch;
    scratchBytes = 0;

    std::lock_guard<std::mutex> lock(m_pendingMutex);

    while (!m_pending.empty()) {
        PendingMesh &candidate = m_pending.back();

        BLAS *blas = candidate.mesh->getBLAS();
        if (blas == nullptr || !blas->isValid()) {
            m_pending.pop_back();
            continue;
        }

        VkDeviceSize next = s_align(scratchBytes, alignment) + blas->getBuildScratchSize();

        // one structure larger than the budget still has to be built, so only a non-empty batch stops here
        if (next > MAX_BATCH_SCRATCH_BYTES && !batch.empty()) {
            break;
        }

        scratchBytes = next;
        batch.push_back(std::move(candidate));
        m_pending.pop_back();
    }

    return batch;
}

CommandBuffer *AccelerationStructureBuilder::takeCommandBuffer()
{
    auto &vulkanContext = Application::getInstance().getVulkanContext();

    // a fiber can resume on a different worker than it parked on, and a pool belongs to one thread
    CommandPoolConfig poolConfig{};
    poolConfig.queueFamilyIndex = vulkanContext.getGraphicsQueueIndex();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolConfig.threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());

    auto &rc = vulkanContext.getRenderContext();
    auto poolHash = rc.commandPoolManager->createCommandPool(poolConfig);

    return rc.commandPoolManager->getCommandPool(poolHash)->getPrimaryCommandBuffer();
}

VkDeviceAddress AccelerationStructureBuilder::reserveScratch(VkDeviceSize sizeBytes)
{
    auto &vulkanContext = Application::getInstance().getVulkanContext();
    const VkDeviceSize alignment = vulkanContext.getAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment;
    const VkDeviceSize requiredBytes = sizeBytes + alignment;

    if (m_scratchCapacityBytes < requiredBytes) {
        if (m_scratchBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, m_scratchBuffer, m_scratchAllocation);
            m_scratchBuffer = VK_NULL_HANDLE;
            m_scratchCapacityBytes = 0;
        }

        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = requiredBytes;
        bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateBuffer(m_allocator, &bufferCreateInfo, &allocCreateInfo, &m_scratchBuffer, &m_scratchAllocation, nullptr) !=
            VK_SUCCESS) {
            RP_CORE_ERROR("Failed to create the acceleration structure build scratch buffer");
            return 0;
        }

        m_scratchCapacityBytes = requiredBytes;
    }

    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = m_scratchBuffer;

    return s_align(vkGetBufferDeviceAddress(m_device, &addressInfo), alignment);
}

void AccelerationStructureBuilder::drainBatch(JobContext &jobContext)
{
    RAPTURE_PROFILE_FUNCTION();

    auto &vulkanContext = Application::getInstance().getVulkanContext();
    const VkDeviceSize alignment = vulkanContext.getAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment;

    while (true) {
        VkDeviceSize scratchBytes = 0;
        std::vector<PendingMesh> batch = takeBatch(scratchBytes);

        if (batch.empty()) {
            releaseBuildResources();
            return;
        }

        VkDeviceAddress scratchAddress = reserveScratch(scratchBytes);
        if (scratchAddress == 0) {
            return;
        }

        const uint32_t count = static_cast<uint32_t>(batch.size());

        if (m_queryPoolCapacity < count) {
            if (m_compactedSizeQueryPool != VK_NULL_HANDLE) {
                vkDestroyQueryPool(m_device, m_compactedSizeQueryPool, nullptr);
            }

            VkQueryPoolCreateInfo queryPoolInfo{};
            queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            queryPoolInfo.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
            queryPoolInfo.queryCount = count;

            if (vkCreateQueryPool(m_device, &queryPoolInfo, nullptr, &m_compactedSizeQueryPool) != VK_SUCCESS) {
                RP_CORE_ERROR("Failed to create the compacted size query pool");
                return;
            }

            m_queryPoolCapacity = count;
        }

        auto graphicsQueue = vulkanContext.getGraphicsQueue();
        TimelineSemaphore semaphore(graphicsQueue->getTimelineSemaphore());

        std::vector<VkAccelerationStructureKHR> structures;
        structures.reserve(count);

        {
            auto buildCommandBuffer = takeCommandBuffer();
            VkCommandBuffer cb = buildCommandBuffer->getCommandBufferVk();

            if (buildCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != VK_SUCCESS) {
                RP_CORE_ERROR("Failed to begin recording the acceleration structure builds");
                return;
            }

            vkCmdResetQueryPool(cb, m_compactedSizeQueryPool, 0, count);

            VkDeviceSize offset = 0;
            for (const PendingMesh &entry : batch) {
                BLAS *blas = entry.mesh->getBLAS();

                offset = s_align(offset, alignment);
                blas->recordBuild(cb, scratchAddress + offset);
                offset += blas->getBuildScratchSize();

                structures.push_back(blas->getAccelerationStructure());
            }

            // the size query reads the structures the builds above write
            VkMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                 VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);

            vulkanContext.vkCmdWriteAccelerationStructuresPropertiesKHR(
                cb, count, structures.data(), VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, m_compactedSizeQueryPool, 0);

            buildCommandBuffer->end();

            uint64_t signalValue = graphicsQueue->addToBatch(buildCommandBuffer);

            Counter gpuCounter;
            gpuCounter.increment();
            jobContext.waitFor(gpuCounter, 0, &semaphore, signalValue);
        }

        for (const PendingMesh &entry : batch) {
            entry.mesh->getBLAS()->markBuilt();
        }

        std::vector<VkDeviceSize> compactedSizes(count, 0);
        if (vkGetQueryPoolResults(m_device, m_compactedSizeQueryPool, 0, count, compactedSizes.size() * sizeof(VkDeviceSize),
                                  compactedSizes.data(), sizeof(VkDeviceSize),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS) {
            RP_CORE_ERROR("Failed to read the compacted acceleration structure sizes, keeping them uncompacted");
            for (const PendingMesh &entry : batch) {
                entry.mesh->getBLAS()->markReady();
            }
            onStructuresReady.fire();
            continue;
        }

        std::vector<PendingMesh> compacting;
        compacting.reserve(count);

        {
            auto copyCommandBuffer = takeCommandBuffer();
            VkCommandBuffer cb = copyCommandBuffer->getCommandBufferVk();

            if (copyCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != VK_SUCCESS) {
                RP_CORE_ERROR("Failed to begin recording the acceleration structure compaction");
                return;
            }

            for (uint32_t i = 0; i < count; ++i) {
                BLAS *blas = batch[i].mesh->getBLAS();

                if (compactedSizes[i] == 0 || !blas->createCompacted(compactedSizes[i])) {
                    blas->markReady();
                    continue;
                }

                blas->recordCompactCopy(cb);
                compacting.push_back(std::move(batch[i]));
            }

            copyCommandBuffer->end();

            if (compacting.empty()) {
                onStructuresReady.fire();
                continue;
            }

            uint64_t signalValue = graphicsQueue->addToBatch(copyCommandBuffer);

            Counter gpuCounter;
            gpuCounter.increment();
            jobContext.waitFor(gpuCounter, 0, &semaphore, signalValue);
        }

        for (const PendingMesh &entry : compacting) {
            entry.mesh->getBLAS()->adoptCompacted();
            entry.mesh->getBLAS()->markReady();
        }

        onStructuresReady.fire();
    }
}

} // namespace Rapture

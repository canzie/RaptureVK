#include "TerrainCuller.h"

#include "AssetManager/AssetImportConfig.h"
#include "AssetManager/AssetManager.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "Renderer/Frustum/Frustum.h"
#include "WindowContext/Application.h"

namespace Rapture {

struct alignas(16) TerrainCullPushConstants {
    glm::vec3 cullOrigin;
    uint32_t chunkCount;

    float heightScale;
    float cullRange;
    uint32_t lodMode;
    uint32_t forcedLOD;

    uint32_t frustumPlanesBufferIndex;
    uint32_t chunkDataBufferIndex;
    uint32_t drawCountBufferIndex;
    uint32_t _pad0;

    uint32_t indirectBufferIndices[TERRAIN_LOD_COUNT];
};

TerrainCuller::TerrainCuller(std::shared_ptr<StorageBuffer> chunkDataBuffer, uint32_t chunkCount, float heightScale,
                             uint32_t initialIndirectCapacity, VmaAllocator allocator)
    : m_chunkDataBuffer(chunkDataBuffer), m_chunkCount(chunkCount), m_heightScale(heightScale),
      m_initialIndirectCapacity(initialIndirectCapacity), m_allocator(allocator), m_commandPoolHash(0)
{
    initCullPipeline();
}

TerrainCuller::~TerrainCuller() {}

void TerrainCuller::initCullPipeline()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    auto &project = app.getProject();
    auto shaderPath = project.getProjectShaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl/terrain/";

    AssetRef asset = AssetManager::importAsset(shaderPath / "glsl/terrain/terrain_cull.cs.glsl", shaderConfig);
    auto shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (!shader || !shader->isReady()) {
        RP_CORE_WARN("TerrainCuller: Cull compute shader not found");
        return;
    }
    m_cullShader = shader;
    m_assets.push_back(std::move(asset));

    ComputePipelineConfiguration pipelineConfig;
    pipelineConfig.shader = m_cullShader;
    m_cullPipeline = std::make_shared<ComputePipeline>(pipelineConfig);

    CommandPoolConfig poolConfig{};
    poolConfig.name = "TerrainCullCommandPool";
    poolConfig.queueFamilyIndex = vc.getComputeQueueIndex();
    poolConfig.flags = 0;

    m_commandPoolHash = CommandPoolManager::createCommandPool(poolConfig);

    RP_CORE_TRACE("TerrainCuller: Cull compute pipeline initialized");
}

TerrainCullBuffers TerrainCuller::createBuffers(const std::vector<uint32_t> &lodsToProcess)
{
    RAPTURE_PROFILE_FUNCTION();

    TerrainCullBuffers buffers;
    buffers.processedLODs = lodsToProcess;
    buffers.indirectBuffers.resize(TERRAIN_LOD_COUNT);
    buffers.indirectCapacities.resize(TERRAIN_LOD_COUNT, 0);

    VkBufferUsageFlags indirectFlags = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    for (uint32_t lod : lodsToProcess) {
        if (lod >= TERRAIN_LOD_COUNT) {
            continue;
        }

        VkDeviceSize bufferSize = m_initialIndirectCapacity * sizeof(VkDrawIndexedIndirectCommand);
        buffers.indirectBuffers[lod] = std::make_unique<StorageBuffer>(bufferSize, BufferUsage::STATIC, m_allocator, indirectFlags);
        buffers.indirectCapacities[lod] = m_initialIndirectCapacity;
    }

    VkDeviceSize countSize = TERRAIN_LOD_COUNT * sizeof(uint32_t);
    VkBufferUsageFlags countFlags = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffers.drawCountBuffer = std::make_unique<StorageBuffer>(countSize, BufferUsage::STATIC, m_allocator, countFlags);

    return buffers;
}

void TerrainCuller::runCull(TerrainCullBuffers &buffers, uint32_t frustumBindlessIndex, const glm::vec3 &cullOrigin)
{
    RAPTURE_PROFILE_FUNCTION();

    if (!m_cullPipeline || m_commandPoolHash == 0) {
        return;
    }

    auto &vc = Application::getInstance().getVulkanContext();

    auto pool = CommandPoolManager::getCommandPool(m_commandPoolHash);
    auto commandBuffer = pool->getPrimaryCommandBuffer();

    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkCommandBuffer cmd = commandBuffer->getCommandBufferVk();

    vkCmdFillBuffer(cmd, buffers.drawCountBuffer->getBufferVk(), 0, VK_WHOLE_SIZE, 0);

    VkMemoryBarrier fillBarrier{};
    fillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fillBarrier, 0, nullptr,
                         0, nullptr);

    m_cullPipeline->bind(cmd);
    DescriptorManager::bindSet(3, commandBuffer, m_cullPipeline);

    bool useForcedLOD = buffers.processedLODs.size() == 1;
    uint32_t forcedLODValue = useForcedLOD ? buffers.processedLODs[0] : 0;

    TerrainCullPushConstants pc{};
    pc.cullOrigin = cullOrigin;
    pc.chunkCount = m_chunkCount;
    pc.heightScale = m_heightScale;
    pc.cullRange = 0.0f;
    pc.lodMode = useForcedLOD ? 1 : 0;
    pc.forcedLOD = forcedLODValue;
    pc.frustumPlanesBufferIndex = frustumBindlessIndex;
    pc.chunkDataBufferIndex = m_chunkDataBuffer->getBindlessIndex();
    pc.drawCountBufferIndex = buffers.drawCountBuffer->getBindlessIndex();

    for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod) {
        if (buffers.indirectBuffers[lod]) {
            pc.indirectBufferIndices[lod] = buffers.indirectBuffers[lod]->getBindlessIndex();
        } else {
            pc.indirectBufferIndices[lod] = UINT32_MAX;
        }
    }

    vkCmdPushConstants(cmd, m_cullPipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TerrainCullPushConstants),
                       &pc);

    uint32_t numGroups = (pc.chunkCount + 63) / 64;
    vkCmdDispatch(cmd, numGroups, 1, 1);

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &barrier, 0, nullptr,
                         0, nullptr);

    commandBuffer->end();

    auto queue = vc.getComputeQueue();
    queue->submitQueue(commandBuffer, VK_NULL_HANDLE);
}

} // namespace Rapture

#include "TerrainGenerator.h"

#include "AssetManager/AssetManager.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Generators/Textures/ProceduralTextures.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "Renderer/Frustum/Frustum.h"
#include "WindowContext/Application.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>

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

static float s_evaluateSpline(const TerrainSpline &spline, float x)
{
    if (spline.points.empty()) return 0.0f;
    if (spline.points.size() == 1) return spline.points[0].y;
    if (x <= spline.points.front().x) return spline.points.front().y;
    if (x >= spline.points.back().x) return spline.points.back().y;

    for (size_t i = 0; i < spline.points.size() - 1; ++i) {
        if (x < spline.points[i + 1].x) {
            float t = (x - spline.points[i].x) / (spline.points[i + 1].x - spline.points[i].x);
            return spline.points[i].y + t * (spline.points[i + 1].y - spline.points[i].y);
        }
    }
    return spline.points.back().y;
}

TerrainGenerator::~TerrainGenerator()
{
    shutdown();
}

void TerrainGenerator::init(const TerrainConfig &config)
{
    if (m_initialized) {
        RP_CORE_WARN("TerrainGenerator already initialized");
        return;
    }

    m_config = config;

    createIndexBuffers();
    createChunkDataBuffer();
    createIndirectBuffer();
    initCullComputePipeline();

    m_initialized = true;

    RP_CORE_INFO("TerrainGenerator initialized: {} world units per chunk, {} height scale", m_config.chunkWorldSize,
                 m_config.heightScale);
}

void TerrainGenerator::shutdown()
{
    if (!m_initialized) {
        return;
    }

    m_chunks.clear();
    m_activeChunkIndices.clear();

    for (uint32_t i = 0; i < TERRAIN_LOD_COUNT; ++i) {
        m_indexBuffers[i].reset();
    }

    m_chunkDataBuffer.reset();
    for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod) {
        m_indirectBuffers[lod].reset();
    }
    m_drawCountBuffer.reset();
    m_cullPipeline.reset();
    m_cullShader.reset();

    m_initialized = false;
    RP_CORE_INFO("TerrainGenerator shutdown");
}

void TerrainGenerator::createIndexBuffers()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod) {
        uint32_t resolution = getTerrainLODResolution(lod);
        uint32_t indexCount = getTerrainLODIndexCount(lod);

        std::vector<uint32_t> indices;
        indices.reserve(indexCount);

        for (uint32_t row = 0; row < resolution - 1; ++row) {
            for (uint32_t col = 0; col < resolution - 1; ++col) {
                uint32_t topLeft = row * resolution + col;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = topLeft + resolution;
                uint32_t bottomRight = bottomLeft + 1;

                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);

                indices.push_back(topLeft);
                indices.push_back(bottomRight);
                indices.push_back(topRight);
            }
        }

        VkDeviceSize bufferSize = indices.size() * sizeof(uint32_t);

        m_indexBuffers[lod] =
            std::make_shared<IndexBuffer>(bufferSize, BufferUsage::STATIC, vc.getVmaAllocator(), VK_INDEX_TYPE_UINT32);
        m_indexBuffers[lod]->addDataGPU(indices.data(), bufferSize, 0);

        RP_CORE_TRACE("TerrainGenerator: Created LOD{} index buffer ({} indices)", lod, indexCount);
    }
}

void TerrainGenerator::createChunkDataBuffer()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    VkDeviceSize bufferSize = m_config.maxLoadedChunks * sizeof(TerrainChunkGPUData);

    m_chunkDataBuffer = std::make_shared<StorageBuffer>(bufferSize, BufferUsage::DYNAMIC, vc.getVmaAllocator());

    RP_CORE_TRACE("TerrainGenerator: Created chunk data buffer for {} chunks", m_config.maxLoadedChunks);
}

void TerrainGenerator::createIndirectBuffer()
{
    auto &vc = Application::getInstance().getVulkanContext();
    VkBufferUsageFlags indirectFlags = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    constexpr uint32_t initialCapacity = 64;
    for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod) {
        VkDeviceSize bufferSize = initialCapacity * sizeof(VkDrawIndexedIndirectCommand);
        m_indirectBuffers[lod] =
            std::make_shared<StorageBuffer>(bufferSize, BufferUsage::STATIC, vc.getVmaAllocator(), indirectFlags);
        m_indirectBufferCapacity[lod] = initialCapacity;
    }

    VkDeviceSize countSize = TERRAIN_LOD_COUNT * sizeof(uint32_t);
    VkBufferUsageFlags countFlags = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    m_drawCountBuffer = std::make_shared<StorageBuffer>(countSize, BufferUsage::STATIC, vc.getVmaAllocator(), countFlags);

    RP_CORE_TRACE("TerrainGenerator: Created indirect command buffers");
}

void TerrainGenerator::initCullComputePipeline()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    auto &project = app.getProject();
    auto shaderPath = project.getProjectShaderDirectory();

    // Load cull compute shader
    auto [shader, handle] = AssetManager::importAsset<Shader>(shaderPath / "glsl/terrain/terrain_cull.cs.glsl");
    if (!shader || !shader->isReady()) {
        RP_CORE_WARN("TerrainGenerator: Cull compute shader not found, using CPU fallback");
        return;
    }
    m_cullShader = shader;

    // Create compute pipeline
    ComputePipelineConfiguration pipelineConfig;
    pipelineConfig.shader = m_cullShader;
    m_cullPipeline = std::make_shared<ComputePipeline>(pipelineConfig);

    // Create command pool
    CommandPoolConfig poolConfig{};
    poolConfig.name = "TerrainCullCommandPool";
    poolConfig.queueFamilyIndex = vc.getComputeQueueIndex();
    poolConfig.flags = 0;

    m_commandPoolHash = CommandPoolManager::createCommandPool(poolConfig);

    RP_CORE_TRACE("TerrainGenerator: Cull compute pipeline initialized");
}

void TerrainGenerator::setNoiseTexture(TerrainNoiseCategory category, std::shared_ptr<Texture> texture)
{
    if (category >= TERRAIN_NC_COUNT) {
        return;
    }
    m_noiseTextures[category] = texture;
}

std::shared_ptr<Texture> TerrainGenerator::getNoiseTexture(TerrainNoiseCategory category) const
{
    if (category >= TERRAIN_NC_COUNT) {
        return nullptr;
    }
    return m_noiseTextures[category];
}

void TerrainGenerator::bakeNoiseLUT()
{
    constexpr uint32_t size = TERRAIN_NOISE_LUT_SIZE;
    std::vector<uint16_t> lutData(size * size * size);

    for (uint32_t z = 0; z < size; ++z) {
        float pv = (static_cast<float>(z) / (size - 1)) * 2.0f - 1.0f;
        float pvFactor = s_evaluateSpline(m_multiNoiseConfig.splines[PEAKS_VALLEYS], pv);

        for (uint32_t y = 0; y < size; ++y) {
            float e = (static_cast<float>(y) / (size - 1)) * 2.0f - 1.0f;
            float eFactor = s_evaluateSpline(m_multiNoiseConfig.splines[EROSION], e);

            for (uint32_t x = 0; x < size; ++x) {
                float c = (static_cast<float>(x) / (size - 1)) * 2.0f - 1.0f;
                float cFactor = s_evaluateSpline(m_multiNoiseConfig.splines[CONTINENTALNESS], c);

                float combined = (cFactor + eFactor + pvFactor) / 3.0f;
                combined = glm::clamp(combined, 0.0f, 1.0f);

                uint32_t idx = z * size * size + y * size + x;
                lutData[idx] = glm::packHalf1x16(combined);
            }
        }
    }

    if (!m_noiseLUT) {
        TextureSpecification spec;
        spec.type = TextureType::TEXTURE3D;
        spec.format = TextureFormat::R16F;
        spec.width = size;
        spec.height = size;
        spec.depth = size;
        spec.filter = TextureFilter::Linear;
        spec.wrap = TextureWrap::ClampToEdge;
        spec.srgb = false;
        m_noiseLUT = std::make_shared<Texture>(spec);
    }

    m_noiseLUT->uploadData(lutData.data(), lutData.size() * sizeof(uint16_t));
}

void TerrainGenerator::generateDefaultNoiseTextures()
{
    ProceduralTextureConfig config;
    config.format = TextureFormat::RGBA8;
    config.filter = TextureFilter::Linear;
    config.wrap = TextureWrap::ClampToEdge;
    config.srgb = false;

    PerlinNoisePushConstants params;

    params.octaves = 4;
    params.scale = 1.5f;
    params.persistence = 0.5f;
    params.lacunarity = 2.0f;
    params.seed = 100;
    m_noiseTextures[CONTINENTALNESS] = ProceduralTexture::generatePerlinNoise(params, config);

    params.octaves = 5;
    params.scale = 4.0f;
    params.persistence = 0.5f;
    params.lacunarity = 2.0f;
    params.seed = 200;
    m_noiseTextures[EROSION] = ProceduralTexture::generatePerlinNoise(params, config);

    RidgedNoisePushConstants ridgedParams;
    ridgedParams.octaves = 6;
    ridgedParams.scale = 8.0f;
    ridgedParams.persistence = 0.55f;
    ridgedParams.lacunarity = 2.0f;
    ridgedParams.seed = 300;
    m_noiseTextures[PEAKS_VALLEYS] = ProceduralTexture::generateRidgedNoise(ridgedParams, config);

    m_multiNoiseConfig.splines[CONTINENTALNESS].points = {{-1.0f, 0.0f}, {1.0f, 1.0f}};
    m_multiNoiseConfig.splines[EROSION].points = {{-1.0f, 0.0f}, {1.0f, 1.0f}};
    m_multiNoiseConfig.splines[PEAKS_VALLEYS].points = {{-1.0f, 0.0f}, {1.0f, 1.0f}};

    bakeNoiseLUT();
}

void TerrainGenerator::loadChunk(glm::ivec2 coord)
{
    if (m_chunks.hasChunk(coord)) {
        return;
    }

    if (m_nextChunkIndex >= m_config.maxLoadedChunks) {
        RP_CORE_WARN("TerrainGenerator: Max chunks reached, cannot load ({}, {})", coord.x, coord.y);
        return;
    }

    TerrainChunk chunk;
    chunk.coord = coord;
    chunk.chunkIndex = m_nextChunkIndex++;
    chunk.state = TerrainChunk::State::Active;
    chunk.lod = 0; // Will be updated in update()

    m_chunks.setChunk(coord, std::move(chunk));
    m_activeChunkIndices.push_back(chunk.chunkIndex);
    m_chunkDataDirty = true;

    RP_CORE_TRACE("TerrainGenerator: Loaded chunk ({}, {}) at index {}", coord.x, coord.y, chunk.chunkIndex);
}

void TerrainGenerator::unloadChunk(glm::ivec2 coord)
{
    auto *chunk = m_chunks.getChunk(coord);
    if (!chunk) {
        return;
    }

    // Remove from active indices
    auto it = std::find(m_activeChunkIndices.begin(), m_activeChunkIndices.end(), chunk->chunkIndex);
    if (it != m_activeChunkIndices.end()) {
        m_activeChunkIndices.erase(it);
    }

    m_chunks.removeChunk(coord);
    m_chunkDataDirty = true;

    RP_CORE_TRACE("TerrainGenerator: Unloaded chunk ({}, {})", coord.x, coord.y);
}

void TerrainGenerator::loadChunksAroundPosition(const glm::vec3 &position, int32_t radius)
{
    glm::ivec2 centerCoord = worldToChunkCoord(position.x, position.z);

    for (int32_t y = -radius; y <= radius; ++y) {
        for (int32_t x = -radius; x <= radius; ++x) {
            loadChunk(centerCoord + glm::ivec2(x, y));
        }
    }
}

void TerrainGenerator::update(const glm::vec3 &cameraPos, Frustum &frustum)
{
    RAPTURE_PROFILE_FUNCTION();

    if (!m_initialized) {
        return;
    }

    if (m_chunkDataDirty) {
        updateChunkGPUData();
        m_chunkDataDirty = false;
    }

    runCullCompute(cameraPos, frustum);
}

void TerrainGenerator::updateChunkGPUData()
{
    RAPTURE_PROFILE_FUNCTION();

    std::vector<TerrainChunkGPUData> gpuData;
    gpuData.resize(m_config.maxLoadedChunks);

    for (auto &[coord, chunk] : m_chunks) {
        if (chunk.chunkIndex >= m_config.maxLoadedChunks) {
            continue;
        }

        TerrainChunkGPUData &data = gpuData[chunk.chunkIndex];
        data.worldOffset = glm::vec2((static_cast<float>(coord.x) - 0.5f) * m_config.chunkWorldSize,
                                     (static_cast<float>(coord.y) - 0.5f) * m_config.chunkWorldSize);
        data.chunkSize = m_config.chunkWorldSize;
        data.lod = chunk.lod;

        // Bounds
        data.bounds = glm::vec4(data.worldOffset.x, data.worldOffset.y, data.worldOffset.x + m_config.chunkWorldSize,
                                data.worldOffset.y + m_config.chunkWorldSize);
        data.minHeight = chunk.minHeight;
        data.maxHeight = chunk.maxHeight;
        data.neighborLODs = 0; // TODO: pack neighbor LODs for seam stitching
        data.flags = 1;        // Visible by default
    }

    // Upload to GPU
    m_chunkDataBuffer->addData(gpuData.data(), gpuData.size() * sizeof(TerrainChunkGPUData), 0);
}

void TerrainGenerator::runCullCompute(const glm::vec3 &cameraPos, Frustum &frustum)
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

    // Zero draw counts on GPU
    vkCmdFillBuffer(cmd, m_drawCountBuffer->getBufferVk(), 0, VK_WHOLE_SIZE, 0);

    VkMemoryBarrier fillBarrier{};
    fillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fillBarrier, 0, nullptr,
                         0, nullptr);

    m_cullPipeline->bind(cmd);
    DescriptorManager::bindSet(3, commandBuffer, m_cullPipeline);

    TerrainCullPushConstants pc{};
    pc.cullOrigin = cameraPos;
    pc.chunkCount = static_cast<uint32_t>(m_chunks.size());
    pc.heightScale = m_config.heightScale;
    pc.cullRange = 0.0f;
    pc.lodMode = 0;
    pc.forcedLOD = 0;
    pc.frustumPlanesBufferIndex = frustum.getBindlessIndex();
    pc.chunkDataBufferIndex = m_chunkDataBuffer->getBindlessIndex();
    pc.drawCountBufferIndex = m_drawCountBuffer->getBindlessIndex();
    for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod) {
        pc.indirectBufferIndices[lod] = m_indirectBuffers[lod]->getBindlessIndex();
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
    // queue->waitIdle();
}

VkBuffer TerrainGenerator::getIndexBuffer(uint32_t lod) const
{
    if (lod >= TERRAIN_LOD_COUNT || !m_indexBuffers[lod]) {
        return VK_NULL_HANDLE;
    }
    return m_indexBuffers[lod]->getBufferVk();
}

uint32_t TerrainGenerator::getVisibleChunkCount(uint32_t lod) const
{
    (void)lod;
    return 0; // GPU-driven: counts are only on GPU
}

uint32_t TerrainGenerator::getTotalVisibleChunks() const
{
    return static_cast<uint32_t>(m_chunks.size()); // Approximate: returns loaded chunks
}

TerrainChunk *TerrainGenerator::getChunkAtWorld(float worldX, float worldZ)
{
    glm::ivec2 coord = worldToChunkCoord(worldX, worldZ);
    return m_chunks.getChunk(coord);
}

TerrainChunk *TerrainGenerator::getChunkAtCoord(glm::ivec2 coord)
{
    return m_chunks.getChunk(coord);
}

glm::ivec2 TerrainGenerator::worldToChunkCoord(float worldX, float worldZ) const
{
    return glm::ivec2(static_cast<int32_t>(std::floor(worldX / m_config.chunkWorldSize)),
                      static_cast<int32_t>(std::floor(worldZ / m_config.chunkWorldSize)));
}

glm::vec2 TerrainGenerator::chunkCoordToWorldCenter(glm::ivec2 coord) const
{
    return glm::vec2(static_cast<float>(coord.x) * m_config.chunkWorldSize, static_cast<float>(coord.y) * m_config.chunkWorldSize);
}

} // namespace Rapture

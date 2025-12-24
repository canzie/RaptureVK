#include "TerrainGenerator.h"

#include "AssetManager/AssetManager.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Generators/Textures/ProceduralTextures.h"
#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include <algorithm>
#include <cmath>

namespace Rapture {

// Push constants matching the compute shader
struct TerrainGenPushConstants {
    glm::vec2 chunkWorldOffset;
    float chunkWorldSize;
    float heightScale;
    uint32_t resolution;
    uint32_t vertexOffset;
    uint32_t indexOffset;
    float terrainWorldSize;
    uint32_t heightmapHandle;
};

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
    initComputePipeline();

    if (m_computeShader == nullptr) {
        return;
    }
    m_initialized = true;

    RP_CORE_INFO("TerrainGenerator initialized: {} world units per chunk, {} height scale, {} resolution", m_config.chunkWorldSize,
                 m_config.heightScale, m_config.chunkResolution);
}

void TerrainGenerator::shutdown()
{
    if (!m_initialized) {
        return;
    }

    m_chunks.clear();
    m_heightmapData.clear();
    m_heightmapTexture.reset();
    m_computePipeline.reset();
    m_computeShader.reset();
    m_commandBuffer.reset();
    m_commandPool.reset();

    m_initialized = false;
    RP_CORE_INFO("TerrainGenerator shutdown");
}

void TerrainGenerator::initComputePipeline()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    auto &project = app.getProject();
    auto shaderPath = project.getProjectShaderDirectory();

    // Load compute shader
    auto [shader, handle] = AssetManager::importAsset<Shader>(shaderPath / "glsl/terrain/terrain_generate.cs.glsl");
    if (!shader or !shader->isReady()) {
        RP_CORE_ERROR("TerrainGenerator: Failed to load compute shader");
        return;
    }
    m_computeShader = shader;

    // Create compute pipeline
    ComputePipelineConfiguration pipelineConfig;
    pipelineConfig.shader = m_computeShader;
    m_computePipeline = std::make_shared<ComputePipeline>(pipelineConfig);

    // Create command pool and buffer
    CommandPoolConfig poolConfig{};
    poolConfig.name = "TerrainGenCommandPool";
    poolConfig.queueFamilyIndex = vc.getComputeQueueIndex();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    m_commandPool = CommandPoolManager::createCommandPool(poolConfig);
    m_commandBuffer = m_commandPool->getCommandBuffer("TerrainGenCmd");

    RP_CORE_TRACE("TerrainGenerator: Compute pipeline initialized");
}

void TerrainGenerator::setHeightmap(std::shared_ptr<Texture> heightmap)
{
    m_heightmapTexture = heightmap;
    RP_CORE_INFO("Heightmap texture set");
}

void TerrainGenerator::generateHeightmap()
{

    ProceduralTextureConfig specConfig;
    specConfig.format = TextureFormat::RGBA32F;
    specConfig.filter = TextureFilter::Linear;
    specConfig.wrap = TextureWrap::ClampToEdge;
    specConfig.srgb = false;
    specConfig.name = "terrain_heightmap";

    m_heightmapTexture = ProceduralTexture::generateWhiteNoise(0, specConfig);
    RP_CORE_TRACE("TerrainGenerator: Created GPU heightmap texture");
}

float TerrainGenerator::sampleHeight(float worldX, float worldZ) const
{
    if (m_heightmapData.empty()) {
        return 0.0f;
    }

    glm::vec2 uv = worldToHeightmapUV(worldX, worldZ);
    float normalizedHeight = sampleHeightmapBilinear(uv.x, uv.y);
    return normalizedHeight * m_config.heightScale;
}

glm::vec3 TerrainGenerator::sampleNormal(float worldX, float worldZ) const
{
    if (m_heightmapData.empty()) {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }

    float delta = 1.0f;
    float hL = sampleHeight(worldX - delta, worldZ);
    float hR = sampleHeight(worldX + delta, worldZ);
    float hD = sampleHeight(worldX, worldZ - delta);
    float hU = sampleHeight(worldX, worldZ + delta);

    glm::vec3 normal(hL - hR, 2.0f * delta, hD - hU);
    return glm::normalize(normal);
}

bool TerrainGenerator::isInBounds(float worldX, float worldZ) const
{
    glm::vec2 uv = worldToHeightmapUV(worldX, worldZ);
    return uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f;
}

void TerrainGenerator::loadChunk(glm::ivec2 coord)
{
    if (m_chunks.hasChunk(coord)) {
        return;
    }

    TerrainChunk chunk;
    chunk.coord = coord;
    chunk.state = TerrainChunk::State::Dirty; // Needs generation

    m_chunks.setChunk(coord, std::move(chunk));

    RP_CORE_TRACE("Loaded chunk ({}, {}) - pending generation", coord.x, coord.y);
}

void TerrainGenerator::unloadChunk(glm::ivec2 coord)
{
    if (!m_chunks.hasChunk(coord)) {
        return;
    }

    m_chunks.removeChunk(coord);
    RP_CORE_TRACE("Unloaded chunk ({}, {})", coord.x, coord.y);
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

void TerrainGenerator::markChunkDirty(glm::ivec2 coord)
{
    auto *chunk = m_chunks.getChunk(coord);
    if (chunk) {
        chunk->state = TerrainChunk::State::Dirty;
    }
}

void TerrainGenerator::update(const glm::vec3 &cameraPos)
{
    if (!m_heightmapTexture) {
        return;
    }

    // Find and regenerate dirty chunks
    auto dirtyChunks = m_chunks.getChunksByState(TerrainChunk::State::Dirty);

    for (auto *chunk : dirtyChunks) {
        generateChunkGeometry(*chunk);
    }

    (void)cameraPos; // TODO: LOD selection based on distance
}

void TerrainGenerator::createChunkBuffers(TerrainChunk &chunk)
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    uint32_t vertexCount = m_config.verticesPerChunk();
    uint32_t indexCount = m_config.indicesPerChunk();

    VkDeviceSize vertexBufferSize = vertexCount * sizeof(TerrainVertex);
    VkDeviceSize indexBufferSize = indexCount * sizeof(uint32_t);

    // Create storage buffers for compute shader output
    // Additional usage flags for vertex/index buffer use
    VkBufferUsageFlags vertexUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VkBufferUsageFlags indexUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    chunk.vertexBuffer = std::make_unique<StorageBuffer>(vertexBufferSize, BufferUsage::STATIC, vc.getVmaAllocator(), vertexUsage);
    chunk.indexBuffer = std::make_unique<StorageBuffer>(indexBufferSize, BufferUsage::STATIC, vc.getVmaAllocator(), indexUsage);

    chunk.vertexCount = vertexCount;
    chunk.indexCount = indexCount;

    DescriptorSetBinding vertexBinding;
    vertexBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    vertexBinding.location = DescriptorSetBindingLocation::CUSTOM_1;
    vertexBinding.count = 1;
    vertexBinding.viewType = TextureViewType::DEFAULT;
    vertexBinding.useStorageImageInfo = true;

    DescriptorSetBinding indexBinding;
    indexBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    indexBinding.location = DescriptorSetBindingLocation::CUSTOM_2;
    indexBinding.count = 1;
    indexBinding.viewType = TextureViewType::DEFAULT;
    indexBinding.useStorageImageInfo = true;

    DescriptorSetBindings bindings;
    bindings.setNumber = 4;
    bindings.bindings.push_back(vertexBinding);
    bindings.bindings.push_back(indexBinding);
    chunk.descriptorSet = std::make_unique<DescriptorSet>(bindings);
    chunk.descriptorSet->getSSBOBinding(DescriptorSetBindingLocation::CUSTOM_1)->add(*chunk.vertexBuffer);
    chunk.descriptorSet->getSSBOBinding(DescriptorSetBindingLocation::CUSTOM_2)->add(*chunk.indexBuffer);
}

void TerrainGenerator::generateChunkGeometry(TerrainChunk &chunk)
{
    if (!m_computePipeline || !m_heightmapTexture) {
        RP_CORE_ERROR("TerrainGenerator: Cannot generate - pipeline or heightmap not ready");
        return;
    }

    chunk.state = TerrainChunk::State::Generating;

    // Create buffers if needed
    if (!chunk.vertexBuffer || !chunk.indexBuffer) {
        createChunkBuffers(chunk);
    }

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    // Record command buffer
    m_commandBuffer->reset();
    m_commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkCommandBuffer cmd = m_commandBuffer->getCommandBufferVk();

    m_computePipeline->bind(cmd);
    chunk.descriptorSet->bind(cmd, m_computePipeline);
    DescriptorManager::bindSet(3, m_commandBuffer, m_computePipeline);

    // Push constants
    TerrainGenPushConstants pc{};
    pc.chunkWorldOffset = glm::vec2(chunk.coord.x * m_config.chunkWorldSize, chunk.coord.y * m_config.chunkWorldSize);
    pc.chunkWorldSize = m_config.chunkWorldSize;
    pc.heightScale = m_config.heightScale;
    pc.resolution = m_config.chunkResolution;
    pc.vertexOffset = 0;
    pc.indexOffset = 0;
    pc.terrainWorldSize = m_config.terrainWorldSize;
    pc.heightmapHandle = m_heightmapTexture->getBindlessIndex();

    vkCmdPushConstants(cmd, m_computePipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(TerrainGenPushConstants), &pc);

    // Dispatch - one workgroup per row, 64 threads per workgroup
    uint32_t workgroupsX = m_config.chunkResolution;
    vkCmdDispatch(cmd, workgroupsX, 1, 1);

    // Memory barrier to ensure compute writes are visible to vertex shader reads
    VkMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &memoryBarrier, 0,
                         nullptr, 0, nullptr);

    m_commandBuffer->end();

    // Submit and wait
    auto queue = vc.getComputeQueue();
    queue->submitQueue(m_commandBuffer);

    chunk.state = TerrainChunk::State::Ready;
    RP_CORE_TRACE("Generated chunk ({}, {}): {} vertices, {} indices", chunk.coord.x, chunk.coord.y, chunk.vertexCount,
                  chunk.indexCount);
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

const TerrainChunk *TerrainGenerator::getChunkAtCoord(glm::ivec2 coord) const
{
    return m_chunks.getChunk(coord);
}

std::vector<TerrainChunk *> TerrainGenerator::getReadyChunks()
{
    return m_chunks.getChunksByState(TerrainChunk::State::Ready);
}

glm::ivec2 TerrainGenerator::worldToChunkCoord(float worldX, float worldZ) const
{
    return glm::ivec2(static_cast<int32_t>(std::floor(worldX / m_config.chunkWorldSize)),
                      static_cast<int32_t>(std::floor(worldZ / m_config.chunkWorldSize)));
}

glm::vec2 TerrainGenerator::chunkCoordToWorldCenter(glm::ivec2 coord) const
{
    return glm::vec2((static_cast<float>(coord.x) + 0.5f) * m_config.chunkWorldSize,
                     (static_cast<float>(coord.y) + 0.5f) * m_config.chunkWorldSize);
}

float TerrainGenerator::sampleHeightmapBilinear(float u, float v) const
{
    if (m_heightmapData.empty()) {
        return 0.0f;
    }

    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    float texX = u * static_cast<float>(m_heightmapWidth - 1);
    float texY = v * static_cast<float>(m_heightmapHeight - 1);

    uint32_t x0 = static_cast<uint32_t>(texX);
    uint32_t y0 = static_cast<uint32_t>(texY);
    uint32_t x1 = std::min(x0 + 1, m_heightmapWidth - 1);
    uint32_t y1 = std::min(y0 + 1, m_heightmapHeight - 1);

    float fx = texX - static_cast<float>(x0);
    float fy = texY - static_cast<float>(y0);

    float h00 = m_heightmapData[y0 * m_heightmapWidth + x0];
    float h10 = m_heightmapData[y0 * m_heightmapWidth + x1];
    float h01 = m_heightmapData[y1 * m_heightmapWidth + x0];
    float h11 = m_heightmapData[y1 * m_heightmapWidth + x1];

    float h0 = h00 * (1.0f - fx) + h10 * fx;
    float h1 = h01 * (1.0f - fx) + h11 * fx;

    return h0 * (1.0f - fy) + h1 * fy;
}

glm::vec2 TerrainGenerator::worldToHeightmapUV(float worldX, float worldZ) const
{
    return glm::vec2(worldX / m_config.terrainWorldSize + 0.5f, worldZ / m_config.terrainWorldSize + 0.5f);
}

} // namespace Rapture

#include "TerrainGenerator.h"

#include "AssetManager/AssetImportConfig.h"
#include "AssetManager/AssetManager.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Generators/Textures/ProceduralTextures.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "Materials/Material.h"
#include "Renderer/Frustum/Frustum.h"
#include "WindowContext/Application.h"

#include <glm/gtc/packing.hpp>

#include <cmath>
#include <thread>

namespace Rapture {

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
    m_chunkCount = m_config.chunkGridSize;

    createIndexBuffers();
    createChunkDataBuffer();
    initComputePipeline();

    auto &vc = Application::getInstance().getVulkanContext();
    m_culler = std::make_unique<TerrainCuller>(m_chunkDataBuffer, m_chunkCount, m_config.heightScale, 64, vc.getVmaAllocator());

    std::vector<uint32_t> allLODs = {0, 1, 2, 3};
    m_cullBuffers = m_culler->createBuffers(allLODs);

    createTerrainMaterials();

    m_initialized = true;

    RP_CORE_INFO("TerrainGenerator initialized: {} chunks (radius {}), {} world units per chunk, {} height scale", m_chunkCount,
                 m_config.getChunkRadius(), m_config.chunkWorldSize, m_config.heightScale);
}

void TerrainGenerator::shutdown()
{
    if (!m_initialized) {
        return;
    }

    for (uint32_t i = 0; i < TERRAIN_LOD_COUNT; ++i) {
        m_indexBuffers[i].reset();
    }

    m_chunkDataBuffer.reset();
    m_culler.reset();

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

    VkDeviceSize bufferSize = m_chunkCount * sizeof(TerrainChunkGPUData);

    m_chunkDataBuffer = std::make_shared<StorageBuffer>(bufferSize, BufferUsage::DYNAMIC, vc.getVmaAllocator());

    RP_CORE_TRACE("TerrainGenerator: Created chunk data buffer for {} chunks", m_chunkCount);
}

void TerrainGenerator::setNoiseTexture(TerrainNoiseCategory category, Texture *texture)
{
    if (category >= TERRAIN_NC_COUNT) {
        return;
    }
    m_noiseTextures[category] = texture;
}

Texture *TerrainGenerator::getNoiseTexture(TerrainNoiseCategory category) const
{
    if (category >= TERRAIN_NC_COUNT) {
        return nullptr;
    }
    return m_noiseTextures[category];
}

void TerrainGenerator::bakeNoiseLUT()
{
    if (!m_initialized || m_config.hmType != HM_CEPV) {
        RP_CORE_WARN("TerrainGenerator: Cannot bake noise LUT for single heightmap");
        return;
    }

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

                float pvAmplitude = 1.0f - eFactor;

                float baseHeight = (cFactor - 0.5f) * 2.0f;
                float pvContrib = (pvFactor - 0.5f) * 2.0f * pvAmplitude;
                float combined = baseHeight * 0.6f + pvContrib * 0.4f;

                combined = combined * 0.5f + 0.5f;
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
        m_noiseLUT = std::make_unique<Texture>(spec);
    }

    m_noiseLUT->uploadData(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(lutData.data()), lutData.size() * sizeof(uint16_t)));
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
    params.scale = 20.0f;
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
    ridgedParams.octaves = 2;
    ridgedParams.scale = 0.8f;
    ridgedParams.persistence = 0.5f;
    ridgedParams.lacunarity = 0.5f;
    ridgedParams.seed = 300;
    ridgedParams.ridgeExponent = 0.6;
    ridgedParams.amplitudeMultiplier = 0.4;
    m_noiseTextures[PEAKS_VALLEYS] = ProceduralTexture::generateRidgedNoise(ridgedParams, config);

    m_multiNoiseConfig.splines[CONTINENTALNESS].points = {{-1.0f, 0.1f}, {-0.4f, 0.3f}, {-0.2f, 0.45f}, {0.0f, 0.5f},
                                                          {0.3f, 0.55f}, {0.6f, 0.7f},  {1.0f, 1.0f}};

    m_multiNoiseConfig.splines[EROSION].points = {{-1.0f, 0.0f}, {-0.5f, 0.2f}, {0.0f, 0.5f}, {0.5f, 0.8f}, {1.0f, 1.0f}};

    m_multiNoiseConfig.splines[PEAKS_VALLEYS].points = {{-1.0f, 0.0f}, {-0.5f, 0.3f}, {0.0f, 0.5f}, {0.5f, 0.7f}, {1.0f, 1.0f}};

    bakeNoiseLUT();
}

void TerrainGenerator::update(const glm::vec3 &cameraPos, Frustum &frustum)
{
    RAPTURE_PROFILE_FUNCTION();
    if (!m_initialized) {
        return;
    }
    // GPU compute: generate chunk grid around camera, compute bounds
    dispatchChunkUpdate(cameraPos);

    // GPU compute: frustum cull chunks, write indirect draw commands
    if (m_culler) {
        m_culler->runCull(m_cullBuffers, frustum.getBindlessIndex(), cameraPos);
    }
}

void TerrainGenerator::initComputePipeline()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    auto &project = app.getProject();
    auto shaderPath = project.getProjectShaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl";

    auto asset = AssetManager::importAsset(shaderPath / "glsl/terrain/terrain_compute_bounds.cs.glsl", shaderConfig);
    Shader *shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (!shader || !shader->isReady()) {
        RP_CORE_WARN("TerrainGenerator: Chunk compute shader not found");
        return;
    }
    m_chunkComputeShader = shader;
    m_assets.push_back(std::move(asset));

    ComputePipelineConfiguration pipelineConfig;
    pipelineConfig.shader = m_chunkComputeShader;
    m_chunkComputePipeline = std::make_shared<ComputePipeline>(pipelineConfig);

    CommandPoolConfig poolConfig{};
    poolConfig.name = "TerrainChunkComputePool";
    poolConfig.queueFamilyIndex = vc.getComputeQueueIndex();
    poolConfig.flags = 0;

    m_computePoolHash = CommandPoolManager::createCommandPool(poolConfig);
}

void TerrainGenerator::dispatchChunkUpdate(const glm::vec3 &cameraPos)
{

    if (!m_chunkComputePipeline || m_computePoolHash == 0) {
        return;
    }

    if (!m_noiseLUT || !m_noiseTextures[CONTINENTALNESS] || !m_noiseTextures[EROSION] || !m_noiseTextures[PEAKS_VALLEYS]) {
        return;
    }

    if (!m_chunkDataBuffer) {
        return;
    }

    auto &vc = Application::getInstance().getVulkanContext();

    auto pool = CommandPoolManager::getCommandPool(m_computePoolHash);
    auto commandBuffer = pool->getPrimaryCommandBuffer();

    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VkCommandBuffer cmd = commandBuffer->getCommandBufferVk();

    m_chunkComputePipeline->bind(cmd);
    DescriptorManager::bindSet(3, commandBuffer, m_chunkComputePipeline);

    struct ChunkUpdatePushConstants {
        uint32_t chunkDataBufferIndex;
        uint32_t continentalnessIndex; // Also used for single heightmap when useMultiNoise = 0
        uint32_t erosionIndex;
        uint32_t peaksValleysIndex;
        uint32_t noiseLUTIndex;
        uint32_t useMultiNoise;
        float heightScale;
        float terrainWorldSize;
        float chunkSize;
        alignas(8) glm::vec2 cameraPos;
        int32_t loadRadius;
        uint32_t sampleResolution;
    } pc;

    static_assert(sizeof(ChunkUpdatePushConstants) == 56, "ChunkUpdatePushConstants must be 64 bytes");

    pc.chunkDataBufferIndex = m_chunkDataBuffer->getBindlessIndex();
    pc.continentalnessIndex = m_noiseTextures[CONTINENTALNESS]->getBindlessIndex();
    pc.useMultiNoise = m_config.hmType == HM_CEPV ? 1u : 0u;

    if (m_config.hmType == HM_CEPV) {
        pc.erosionIndex = m_noiseTextures[EROSION]->getBindlessIndex();
        pc.peaksValleysIndex = m_noiseTextures[PEAKS_VALLEYS]->getBindlessIndex();
        pc.noiseLUTIndex = m_noiseLUT->getBindlessIndex();
    } else {
        pc.erosionIndex = 0;
        pc.peaksValleysIndex = 0;
        pc.noiseLUTIndex = 0;
    }
    pc.heightScale = m_config.heightScale;
    pc.terrainWorldSize = m_config.terrainWorldSize;
    pc.chunkSize = m_config.chunkWorldSize;
    pc.cameraPos = glm::vec2(cameraPos.x, cameraPos.z);
    pc.loadRadius = m_config.getChunkRadius();
    pc.sampleResolution = 16;

    vkCmdPushConstants(cmd, m_chunkComputePipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(ChunkUpdatePushConstants), &pc);

    uint32_t numGroups = (m_chunkCount + 63) / 64;
    vkCmdDispatch(cmd, numGroups, 1, 1);

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0,
                         nullptr, 0, nullptr);

    commandBuffer->end();

    auto queue = vc.getComputeQueue();
    queue->submitQueue(commandBuffer, nullptr, nullptr);
}

VkBuffer TerrainGenerator::getIndexBuffer(uint32_t lod) const
{
    if (lod >= TERRAIN_LOD_COUNT || !m_indexBuffers[lod]) {
        return VK_NULL_HANDLE;
    }
    return m_indexBuffers[lod]->getBufferVk();
}

void TerrainGenerator::createTerrainMaterials()
{
    auto terrainBase = MaterialManager::getMaterial("Terrain");
    if (!terrainBase) {
        RP_CORE_ERROR("Terrain base material not found");
        return;
    }

    m_grassMaterial = std::make_shared<MaterialInstance>(terrainBase, "TerrainGrass");
    m_grassMaterial->setParameter(ParameterID::ALBEDO, glm::vec4(19.0f / 255.0f, 109.0f / 255.0f, 21.0f / 255.0f, 1.0f));
    m_grassMaterial->setParameter(ParameterID::ROUGHNESS, 0.9f);
    m_grassMaterial->setParameter(ParameterID::METALLIC, 0.0f);
    m_grassMaterial->setParameter(ParameterID::TILING_SCALE, 0.1f);
    m_grassMaterial->setParameter(ParameterID::SLOPE_THRESHOLD, 0.4f);
    m_grassMaterial->setParameter(ParameterID::HEIGHT_BLEND, 0.75f);

    m_rockMaterial = std::make_shared<MaterialInstance>(terrainBase, "TerrainRock");
    m_rockMaterial->setParameter(ParameterID::ALBEDO, glm::vec4(0.4f, 0.35f, 0.3f, 1.0f));
    m_rockMaterial->setParameter(ParameterID::ROUGHNESS, 0.85f);
    m_rockMaterial->setParameter(ParameterID::METALLIC, 0.0f);
    m_rockMaterial->setParameter(ParameterID::TILING_SCALE, 0.15f);

    m_snowMaterial = std::make_shared<MaterialInstance>(terrainBase, "TerrainSnow");
    m_snowMaterial->setParameter(ParameterID::ALBEDO, glm::vec4(0.95f, 0.95f, 0.98f, 1.0f));
    m_snowMaterial->setParameter(ParameterID::ROUGHNESS, 0.3f);
    m_snowMaterial->setParameter(ParameterID::METALLIC, 0.0f);
    m_snowMaterial->setParameter(ParameterID::TILING_SCALE, 0.2f);

    RP_CORE_INFO("Terrain materials created: grass={}, rock={}, snow={}", m_grassMaterial->getBindlessIndex(),
                 m_rockMaterial->getBindlessIndex(), m_snowMaterial->getBindlessIndex());
}

uint32_t TerrainGenerator::getGrassMaterialIndex() const
{
    return m_grassMaterial ? m_grassMaterial->getBindlessIndex() : 0;
}

uint32_t TerrainGenerator::getRockMaterialIndex() const
{
    return m_rockMaterial ? m_rockMaterial->getBindlessIndex() : 0;
}

uint32_t TerrainGenerator::getSnowMaterialIndex() const
{
    return m_snowMaterial ? m_snowMaterial->getBindlessIndex() : 0;
}

} // namespace Rapture

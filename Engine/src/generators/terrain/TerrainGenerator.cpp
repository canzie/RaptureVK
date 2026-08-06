#include "TerrainGenerator.h"

#include "utils/EnginePaths.h"

#include "asset_manager/AssetImportConfig.h"
#include "asset_manager/AssetManager.h"
#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/command_buffers/CommandPool.h"
#include "buffers/descriptors/DescriptorManager.h"
#include "generators/textures/ProceduralTextures.h"
#include "logging/Log.h"
#include "logging/TracyProfiler.h"
#include "materials/Material.h"
#include "materials/graph/SurfaceGraphManager.h"
#include "renderer/Frustum.h"
#include "window_context/Application.h"

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
    uint32_t framesInFlight = Application::getInstance().getFramesInFlight();
    m_cullBuffers.resize(framesInFlight);
    for (auto &buffers : m_cullBuffers) {
        buffers = m_culler->createBuffers(allLODs);
    }

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

void TerrainGenerator::setNoiseTexture(TerrainNoiseCategory category, AssetPtr<Texture> texture)
{
    if (category >= TERRAIN_NC_COUNT) {
        return;
    }
    m_noiseTextures[category] = std::move(texture);
}

Texture *TerrainGenerator::getNoiseTexture(TerrainNoiseCategory category) const
{
    if (category >= TERRAIN_NC_COUNT) {
        return nullptr;
    }
    return m_noiseTextures[category].get();
}

void TerrainGenerator::bakeSplineCurves()
{
    if (!m_initialized || m_config.hmType != HM_CEPV) {
        RP_CORE_WARN("TerrainGenerator: Cannot bake spline curves for single heightmap");
        return;
    }

    constexpr uint32_t res = TERRAIN_SPLINE_CURVE_RESOLUTION;
    constexpr uint32_t rows = TERRAIN_NC_COUNT;
    std::vector<uint16_t> curveData(res * rows);

    for (uint32_t cat = 0; cat < rows; ++cat) {
        const TerrainSpline &spline = m_multiNoiseConfig.splines[cat];
        for (uint32_t i = 0; i < res; ++i) {
            float x = (static_cast<float>(i) / (res - 1)) * 2.0f - 1.0f;
            float y = glm::clamp(s_evaluateSpline(spline, x), 0.0f, 1.0f);
            curveData[cat * res + i] = glm::packHalf1x16(y);
        }
    }

    if (!m_splineCurveTexture) {
        TextureSpecification spec;
        spec.type = TextureType::TEXTURE2D;
        spec.format = TextureFormat::R16F;
        spec.width = res;
        spec.height = rows;
        spec.filter = TextureFilter::Linear;
        spec.wrap = TextureWrap::ClampToEdge;
        spec.srgb = false;
        m_splineCurveTexture = std::make_unique<Texture>(spec);
    }

    m_splineCurveTexture->uploadData(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(curveData.data()), curveData.size() * sizeof(uint16_t)));
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

    bakeSplineCurves();
}

void TerrainGenerator::update(const glm::vec3 &cameraPos, Frustum &frustum, uint32_t frameIndex)
{
    RAPTURE_PROFILE_FUNCTION();
    if (!m_initialized) {
        return;
    }
    // GPU compute: generate chunk grid around camera, compute bounds
    dispatchChunkUpdate(cameraPos);

    // GPU compute: frustum cull chunks, write indirect draw commands
    if (m_culler && frameIndex < m_cullBuffers.size()) {
        frustum.uploadFrustum(frameIndex);
        m_culler->runCull(m_cullBuffers[frameIndex], frustum.getBindlessIndex(frameIndex), cameraPos);
    }
}

void TerrainGenerator::initComputePipeline()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    auto shaderPath = EnginePaths::shaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl";

    auto asset = AssetManager::importAsset(shaderPath / "glsl/terrain/terrain_compute_bounds.cs.glsl", shaderConfig);
    Shader *shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (!shader || !shader->isReady()) {
        RP_CORE_WARN("TerrainGenerator: Chunk compute shader not found");
        return;
    }
    m_chunkComputeShader = shader;
    m_shaderAssets.push_back(std::move(asset));

    ComputePipelineConfiguration pipelineConfig;
    pipelineConfig.shader = m_chunkComputeShader;
    m_chunkComputePipeline = std::make_shared<ComputePipeline>(pipelineConfig);

    CommandPoolConfig poolConfig{};
    poolConfig.name = "TerrainChunkComputePool";
    poolConfig.queueFamilyIndex = vc.getComputeQueueIndex();
    poolConfig.flags = 0;

    auto& rc = vc.getRenderContext();
    m_computePoolHash = rc.commandPoolManager->createCommandPool(poolConfig);
}

void TerrainGenerator::dispatchChunkUpdate(const glm::vec3 &cameraPos)
{

    if (!m_chunkComputePipeline || m_computePoolHash == 0) {
        return;
    }

    if (!m_splineCurveTexture || !m_noiseTextures[CONTINENTALNESS] || !m_noiseTextures[EROSION] || !m_noiseTextures[PEAKS_VALLEYS]) {
        return;
    }

    if (!m_chunkDataBuffer) {
        return;
    }

    auto &vc = Application::getInstance().getVulkanContext();

    auto& rc = vc.getRenderContext();
    auto pool = rc.commandPoolManager->getCommandPool(m_computePoolHash);
    auto commandBuffer = pool->getPrimaryCommandBuffer();

    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VkCommandBuffer cmd = commandBuffer->getCommandBufferVk();

    m_chunkComputePipeline->bind(cmd);
    rc.descriptorManager->bindSet(3, commandBuffer, m_chunkComputePipeline);

    struct ChunkUpdatePushConstants {
        uint32_t chunkDataBufferIndex;
        uint32_t continentalnessIndex; // Also used for single heightmap when useMultiNoise = 0
        uint32_t erosionIndex;
        uint32_t peaksValleysIndex;
        uint32_t splineCurveIndex;
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
        pc.splineCurveIndex = m_splineCurveTexture->getBindlessIndex();
    } else {
        pc.erosionIndex = 0;
        pc.peaksValleysIndex = 0;
        pc.splineCurveIndex = 0;
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

/**
 * @brief A layered natural terrain shaded from the terrain domain's inputs, with no textures
 *
 * Four signals drive it: erosion tints grass from lush to dry, curvature darkens the concavities
 * sediment collects in, slope exposes rock on steep faces, and height puts sand at the bottom and
 * snow on the peaks. Snow is masked off anything steep enough to be rock, so cliffs stay bare.
 * @return The authored graph
 */
static MaterialGraph s_buildTerrainGraph()
{
    using GN = GraphNodeType;
    using PV = PinValue;
    MaterialGraph graph;
    graph.name = "TerrainLayered";
    graph.domain = GD_TERRAIN;

    graph.nodes.push_back({.id = 1, .type = GN::SLOPE});
    graph.nodes.push_back({.id = 2, .type = GN::TERRAIN_HEIGHT});
    graph.nodes.push_back({.id = 3, .type = GN::TERRAIN_EROSION});
    graph.nodes.push_back({.id = 4, .type = GN::TERRAIN_CURVATURE});

    // Grass tinted from lush to dry by erosion
    graph.nodes.push_back({.id = 5, .type = GN::CONSTANT_VEC3, .inputValues = {PV(glm::vec3(0.04f, 0.17f, 0.04f))}});
    graph.nodes.push_back({.id = 6, .type = GN::CONSTANT_VEC3, .inputValues = {PV(glm::vec3(0.14f, 0.25f, 0.07f))}});
    graph.nodes.push_back({.id = 7, .type = GN::MIX_VEC3});

    // Concavities collect darker sediment
    graph.nodes.push_back({.id = 8, .type = GN::CONSTANT_FLOAT, .inputValues = {PV(6.0f)}});
    graph.nodes.push_back({.id = 9, .type = GN::MULTIPLY_FLOAT});
    graph.nodes.push_back({.id = 10, .type = GN::SATURATE_FLOAT});
    graph.nodes.push_back({.id = 11, .type = GN::CONSTANT_VEC3, .inputValues = {PV(glm::vec3(0.18f, 0.14f, 0.09f))}});
    graph.nodes.push_back({.id = 12, .type = GN::MIX_VEC3});

    // Rock on the steep faces
    graph.nodes.push_back({.id = 13, .type = GN::CONSTANT_FLOAT, .inputValues = {PV(0.30f)}});
    graph.nodes.push_back({.id = 14, .type = GN::CONSTANT_FLOAT, .inputValues = {PV(0.55f)}});
    graph.nodes.push_back({.id = 15, .type = GN::SMOOTHSTEP_FLOAT});
    graph.nodes.push_back({.id = 16, .type = GN::CONSTANT_VEC3, .inputValues = {PV(glm::vec3(0.33f, 0.30f, 0.28f))}});
    graph.nodes.push_back({.id = 17, .type = GN::MIX_VEC3});

    graph.nodes.push_back({.id = 21, .type = GN::CONSTANT_FLOAT, .inputValues = {PV(1.0f)}});

    // Snow on the peaks, kept off anything steep enough to be rock
    graph.nodes.push_back({.id = 25, .type = GN::CONSTANT_FLOAT, .inputValues = {PV(0.60f)}});
    graph.nodes.push_back({.id = 26, .type = GN::CONSTANT_FLOAT, .inputValues = {PV(0.72f)}});
    graph.nodes.push_back({.id = 27, .type = GN::SMOOTHSTEP_FLOAT});
    graph.nodes.push_back({.id = 28, .type = GN::SUBTRACT_FLOAT});
    graph.nodes.push_back({.id = 29, .type = GN::MULTIPLY_FLOAT});
    graph.nodes.push_back({.id = 30, .type = GN::CONSTANT_VEC3, .inputValues = {PV(glm::vec3(0.90f, 0.93f, 0.97f))}});
    graph.nodes.push_back({.id = 31, .type = GN::MIX_VEC3});

    // Snow reads smoother than the ground it covers
    graph.nodes.push_back({.id = 32, .type = GN::CONSTANT_FLOAT, .inputValues = {PV(0.92f)}});
    graph.nodes.push_back({.id = 33, .type = GN::CONSTANT_FLOAT, .inputValues = {PV(0.38f)}});
    graph.nodes.push_back({.id = 34, .type = GN::MIX_FLOAT});

    graph.nodes.push_back({.id = 35, .type = GN::SURFACE_OUTPUT});
    graph.outputNodeId = 35;

    graph.connections = {
        {5, 0, 7, 0},   {6, 0, 7, 1},   {3, 0, 7, 2},   // grass = mix(lush, dry, erosion)
        {4, 0, 9, 0},   {8, 0, 9, 1},   {9, 0, 10, 0},  // sediment mask = saturate(curvature * gain)
        {7, 0, 12, 0},  {11, 0, 12, 1}, {10, 0, 12, 2}, // ground = mix(grass, sediment, sediment mask)
        {13, 0, 15, 0}, {14, 0, 15, 1}, {1, 0, 15, 2},  // rock mask = smoothstep(slope)
        {12, 0, 17, 0}, {16, 0, 17, 1}, {15, 0, 17, 2}, // ground = mix(ground, rock, rock mask)
        {25, 0, 27, 0}, {26, 0, 27, 1}, {2, 0, 27, 2},  // high mask = smoothstep(height)
        {21, 0, 28, 0}, {15, 0, 28, 1},                 // not steep = 1 - rock mask
        {27, 0, 29, 0}, {28, 0, 29, 1},                 // snow mask = high mask * not steep
        {17, 0, 31, 0}, {30, 0, 31, 1}, {29, 0, 31, 2}, // albedo = mix(ground, snow, snow mask)
        {32, 0, 34, 0}, {33, 0, 34, 1}, {29, 0, 34, 2}, // roughness = mix(ground, snow, snow mask)
        {31, 0, 35, 0}, {34, 0, 35, 2},                 // -> albedo, roughness
    };

    return graph;
}

void TerrainGenerator::createTerrainMaterials()
{
    // every terrain shades the same way, so a second generator adopts the instance the first one made
    if (AssetRef existing = AssetManager::getVirtualAsset("Terrain")) {
        m_material = AssetPtr<MaterialInstance>(std::move(existing));
        return;
    }

    auto terrainBase = MaterialManager::getMaterial("Terrain Base Material");
    if (!terrainBase) {
        RP_CORE_ERROR("Terrain base material not found");
        return;
    }

    auto instance = std::make_unique<MaterialInstance>(terrainBase, "Terrain");
    m_material = AssetPtr<MaterialInstance>(AssetManager::registerVirtualAsset(std::move(instance), "Terrain", AssetType::MATERIAL_INSTANCE));

    SurfaceGraphManager &graphs = MaterialManager::getSurfaceGraphManager();
    uint32_t graphId = graphs.registerGraph(s_buildTerrainGraph());
    if (graphId == UINT32_MAX) {
        RP_CORE_ERROR("Failed to compile the terrain material graph, terrain keeps its base material");
        return;
    }
    m_material->setGraph(graphId, graphs.getDefaults(graphId), graphs.getTextureRefs(graphId));

    // the generated dispatcher switches on the id this registration just handed out, so it is rewritten to match
    graphs.writeGeneratedFiles(EnginePaths::shaderDirectory() / "glsl/generated");

    RP_CORE_INFO("Terrain material created: index={}, graph={}", m_material->getBindlessIndex(), graphId);
}

uint32_t TerrainGenerator::getMaterialIndex() const
{
    return m_material ? m_material->getBindlessIndex() : UINT32_MAX;
}

} // namespace Rapture

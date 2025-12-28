#include "TerrainGenerator.h"

#include "Buffers/Descriptors/DescriptorManager.h"
#include "Generators/Textures/ProceduralTextures.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "Materials/Material.h"
#include "Renderer/Frustum/Frustum.h"
#include "WindowContext/Application.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>

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

    createIndexBuffers();
    createChunkDataBuffer();

    auto &vc = Application::getInstance().getVulkanContext();
    m_culler = std::make_unique<TerrainCuller>(m_chunkDataBuffer, &m_chunks, m_config.heightScale, 64, vc.getVmaAllocator());

    std::vector<uint32_t> allLODs = {0, 1, 2, 3};
    m_cullBuffers = m_culler->createBuffers(allLODs);

    createTerrainMaterials();

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

    VkDeviceSize bufferSize = m_config.maxLoadedChunks * sizeof(TerrainChunkGPUData);

    m_chunkDataBuffer = std::make_shared<StorageBuffer>(bufferSize, BufferUsage::DYNAMIC, vc.getVmaAllocator());

    RP_CORE_TRACE("TerrainGenerator: Created chunk data buffer for {} chunks", m_config.maxLoadedChunks);
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

                // Minecraft-style combination:
                // Continentalness: base terrain shape (0=ocean, 0.5=coast, 1=inland)
                // Erosion: controls amplitude (0=mountains allowed, 1=flat/eroded)
                // PV: local peaks and valleys, scaled by erosion

                // Base height from continentalness, centered at 0
                float baseHeight = (cFactor - 0.5f) * 2.0f;

                // Erosion controls how much PV can contribute
                // High erosion (1) = flat terrain, low PV influence
                // Low erosion (0) = rugged terrain, full PV influence
                float pvAmplitude = 1.0f - eFactor;

                // PV adds peaks/valleys, centered at 0, scaled by erosion
                float pvContrib = (pvFactor - 0.5f) * 2.0f * pvAmplitude;

                // Combine: continentalness dominates, PV adds detail
                float combined = baseHeight * 0.6f + pvContrib * 0.4f;

                // Map to 0-1 for storage (shader does -0.5 to center at 0)
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
    ridgedParams.octaves = 4;
    ridgedParams.scale = 8.0f;
    ridgedParams.persistence = 0.55f;
    ridgedParams.lacunarity = 2.0f;
    ridgedParams.seed = 1;
    m_noiseTextures[PEAKS_VALLEYS] = ProceduralTexture::generateRidgedNoise(ridgedParams, config);

    // Continentalness: ocean -> coast -> plains -> hills -> mountains
    // Low values = below sea level, high values = elevated terrain
    m_multiNoiseConfig.splines[CONTINENTALNESS].points = {
        {-1.0f, 0.1f},  // Deep ocean
        {-0.4f, 0.3f},  // Shallow ocean
        {-0.2f, 0.45f}, // Coast/beach
        {0.0f, 0.5f},   // Sea level
        {0.3f, 0.55f},  // Plains
        {0.6f, 0.7f},   // Hills
        {1.0f, 1.0f}    // Mountains
    };

    // Erosion: controls terrain roughness
    // Low erosion = rugged mountains, high erosion = flat plains
    m_multiNoiseConfig.splines[EROSION].points = {
        {-1.0f, 0.0f}, // No erosion - full mountain amplitude
        {-0.5f, 0.2f}, // Light erosion
        {0.0f, 0.5f},  // Medium erosion
        {0.5f, 0.8f},  // Heavy erosion
        {1.0f, 1.0f}   // Fully eroded - flat
    };

    // Peaks/Valleys: local height variation
    // Creates the actual bumps and dips
    m_multiNoiseConfig.splines[PEAKS_VALLEYS].points = {
        {-1.0f, 0.0f}, // Deep valley
        {-0.5f, 0.3f}, // Shallow valley
        {0.0f, 0.5f},  // Neutral
        {0.5f, 0.7f},  // Small peak
        {1.0f, 1.0f}   // Tall peak
    };

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

    if (m_culler) {
        m_culler->runCull(m_cullBuffers, frustum.getBindlessIndex(), cameraPos);
    }
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

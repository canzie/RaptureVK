#ifndef RAPTURE__TERRAIN_GENERATOR_H
#define RAPTURE__TERRAIN_GENERATOR_H

#include "ChunkGrid.h"
#include "TerrainCuller.h"
#include "TerrainTypes.h"

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/IndexBuffers/IndexBuffer.h"
#include "Buffers/StorageBuffers/StorageBuffer.h"
#include "Generators/Textures/ProceduralTextures.h"
#include "Pipelines/ComputePipeline.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"
#include "Materials/MaterialInstance.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace Rapture {

class Frustum;

/**
 * @brief Manages terrain chunk loading, LOD selection, and GPU culling.
 *
 */
class TerrainGenerator {
  public:
    TerrainGenerator() = default;
    ~TerrainGenerator();

    TerrainGenerator(const TerrainGenerator &) = delete;
    TerrainGenerator &operator=(const TerrainGenerator &) = delete;

    // Lifecycle
    void init(const TerrainConfig &config);
    void shutdown();

    void setNoiseTexture(TerrainNoiseCategory category, std::shared_ptr<Texture> texture);
    std::shared_ptr<Texture> getNoiseTexture(TerrainNoiseCategory category) const;
    MultiNoiseConfig &getMultiNoiseConfig() { return m_multiNoiseConfig; }
    const MultiNoiseConfig &getMultiNoiseConfig() const { return m_multiNoiseConfig; }
    void bakeNoiseLUT();
    std::shared_ptr<Texture> getNoiseLUT() const { return m_noiseLUT; }
    void generateDefaultNoiseTextures();

    // Chunk management
    void loadChunk(glm::ivec2 coord);
    void unloadChunk(glm::ivec2 coord);
    void loadChunksAroundPosition(const glm::vec3 &position, int32_t radius);

    // Per-frame update: LOD selection, culling, build indirect commands
    void update(const glm::vec3 &cameraPos, Frustum &frustum);

    // Rendering resources (used by GBufferPass)
    std::shared_ptr<StorageBuffer> getChunkDataBuffer() const { return m_chunkDataBuffer; }
    VkBuffer getIndexBuffer(uint32_t lod) const;
    uint32_t getIndexCount(uint32_t lod) const { return getTerrainLODIndexCount(lod); }

    TerrainCuller *getTerrainCuller() { return m_culler.get(); }
    TerrainCullBuffers *getCullBuffers() { return &m_cullBuffers; }

    // Get visible chunk count per LOD (after update)
    uint32_t getVisibleChunkCount(uint32_t lod) const;
    uint32_t getTotalVisibleChunks() const;

    // Queries
    TerrainChunk *getChunkAtWorld(float worldX, float worldZ);
    TerrainChunk *getChunkAtCoord(glm::ivec2 coord);
    glm::ivec2 worldToChunkCoord(float worldX, float worldZ) const;
    glm::vec2 chunkCoordToWorldCenter(glm::ivec2 coord) const;

    // Accessors
    const TerrainConfig &getConfig() const { return m_config; }
    TerrainConfig &getConfigMutable() { return m_config; }
    size_t getLoadedChunkCount() const { return m_chunks.size(); }
    bool isInitialized() const { return m_initialized; }

    void setHeightScale(float scale) { m_config.heightScale = scale; }

    // Debug
    void setWireframe(bool enabled) { m_wireframe = enabled; }
    bool isWireframe() const { return m_wireframe; }

    // Materials
    uint32_t getGrassMaterialIndex() const;
    uint32_t getRockMaterialIndex() const;
    uint32_t getSnowMaterialIndex() const;

  private:
    void createTerrainMaterials();
    void createIndexBuffers();
    void createChunkDataBuffer();

    void updateChunkGPUData();

    TerrainConfig m_config;
    ChunkGrid m_chunks;

    // Active chunks list (indices into chunk data buffer)
    std::vector<uint32_t> m_activeChunkIndices;
    uint32_t m_nextChunkIndex = 0;

    MultiNoiseConfig m_multiNoiseConfig;
    std::shared_ptr<Texture> m_noiseTextures[TERRAIN_NC_COUNT];
    std::shared_ptr<Texture> m_noiseLUT;

    // Shared index buffers (one per LOD, grid topology)
    std::shared_ptr<IndexBuffer> m_indexBuffers[TERRAIN_LOD_COUNT];

    std::shared_ptr<StorageBuffer> m_chunkDataBuffer;
    bool m_chunkDataDirty = true;

    std::unique_ptr<TerrainCuller> m_culler;
    TerrainCullBuffers m_cullBuffers;

    bool m_initialized = false;
    bool m_wireframe = false;

    std::shared_ptr<MaterialInstance> m_grassMaterial;
    std::shared_ptr<MaterialInstance> m_rockMaterial;
    std::shared_ptr<MaterialInstance> m_snowMaterial;
};

} // namespace Rapture

#endif // RAPTURE__TERRAIN_GENERATOR_H

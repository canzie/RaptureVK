#ifndef RAPTURE__TERRAIN_GENERATOR_H
#define RAPTURE__TERRAIN_GENERATOR_H

#include "ChunkGrid.h"
#include "TerrainTypes.h"

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/IndexBuffers/IndexBuffer.h"
#include "Buffers/StorageBuffers/StorageBuffer.h"
#include "Generators/Textures/ProceduralTextures.h"
#include "Pipelines/ComputePipeline.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"

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

    // Heightmap management
    void setHeightmap(std::shared_ptr<Texture> heightmap);
    void generateHeightmap();
    bool hasHeightmap() const { return m_heightmapTexture != nullptr; }

    PerlinNoisePushConstants &getNoiseParams() { return m_noiseParams; }

    // CPU height sampling (for physics, object placement)
    float sampleHeight(float worldX, float worldZ) const;
    glm::vec3 sampleNormal(float worldX, float worldZ) const;
    bool isInBounds(float worldX, float worldZ) const;

    // Chunk management
    void loadChunk(glm::ivec2 coord);
    void unloadChunk(glm::ivec2 coord);
    void loadChunksAroundPosition(const glm::vec3 &position, int32_t radius);

    // Per-frame update: LOD selection, culling, build indirect commands
    void update(const glm::vec3 &cameraPos, Frustum &frustum);

    // Rendering resources (used by GBufferPass)
    std::shared_ptr<StorageBuffer> getChunkDataBuffer() const { return m_chunkDataBuffer; }
    std::shared_ptr<StorageBuffer> getIndirectBuffer(uint32_t lod) const { return m_indirectBuffers[lod]; }
    std::shared_ptr<StorageBuffer> getDrawCountBuffer() const { return m_drawCountBuffer; }
    VkBuffer getIndexBuffer(uint32_t lod) const;
    uint32_t getIndexCount(uint32_t lod) const { return getTerrainLODIndexCount(lod); }
    std::shared_ptr<Texture> getHeightmapTexture() const { return m_heightmapTexture; }
    uint32_t getIndirectBufferCapacity(uint32_t lod) const { return m_indirectBufferCapacity[lod]; }

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

  private:
    void createIndexBuffers();
    void createChunkDataBuffer();
    void createIndirectBuffer();
    void initCullComputePipeline();

    void updateChunkGPUData();
    void runCullCompute(const glm::vec3 &cameraPos, Frustum &frustum);

    // Height sampling helpers
    float sampleHeightmapBilinear(float u, float v) const;
    glm::vec2 worldToHeightmapUV(float worldX, float worldZ) const;

    TerrainConfig m_config;
    ChunkGrid m_chunks;

    // Active chunks list (indices into chunk data buffer)
    std::vector<uint32_t> m_activeChunkIndices;
    uint32_t m_nextChunkIndex = 0;

    // Heightmap data (CPU-side for sampling)
    std::vector<float> m_heightmapData;
    uint32_t m_heightmapWidth = 0;
    uint32_t m_heightmapHeight = 0;

    // GPU heightmap texture (for vertex shader sampling)
    std::shared_ptr<Texture> m_heightmapTexture;
    std::unique_ptr<ProceduralTexture> m_heightmapGenerator = nullptr;
    PerlinNoisePushConstants m_noiseParams;

    // Shared index buffers (one per LOD, grid topology)
    std::shared_ptr<IndexBuffer> m_indexBuffers[TERRAIN_LOD_COUNT];

    // Chunk data SSBO (TerrainChunkGPUData array)
    std::shared_ptr<StorageBuffer> m_chunkDataBuffer;
    bool m_chunkDataDirty = true;

    // Indirect draw commands per LOD (built by compute shader)
    std::shared_ptr<StorageBuffer> m_indirectBuffers[TERRAIN_LOD_COUNT];
    std::shared_ptr<StorageBuffer> m_drawCountBuffer;
    uint32_t m_indirectBufferCapacity[TERRAIN_LOD_COUNT] = {0};

    // Cull/LOD compute shader
    std::shared_ptr<Shader> m_cullShader;
    std::shared_ptr<ComputePipeline> m_cullPipeline;
    CommandPoolHash m_commandPoolHash = 0;

    bool m_initialized = false;
    bool m_wireframe = false;
};

} // namespace Rapture

#endif // RAPTURE__TERRAIN_GENERATOR_H

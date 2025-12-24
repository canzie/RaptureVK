#ifndef RAPTURE__TERRAIN_GENERATOR_H
#define RAPTURE__TERRAIN_GENERATOR_H

#include "ChunkGrid.h"
#include "TerrainTypes.h"

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Pipelines/ComputePipeline.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace Rapture {

/**
 * @brief Manages terrain generation, chunk loading, and height sampling.
 *
 * Uses compute shaders to generate terrain geometry into storage buffers.
 * Geometry is cached and only regenerated when LOD changes.
 *
 * Usage:
 *   TerrainGenerator terrain;
 *   terrain.init({.chunkWorldSize = 64.0f, .heightScale = 100.0f});
 *   terrain.generateHeightmap();  // or setHeightmap()
 *   terrain.loadChunksAroundPosition(cameraPos, 3);
 *
 *   // Per frame - regenerates dirty chunks
 *   terrain.update(cameraPos);
 *
 *   // Get ready chunks for rendering
 *   auto chunks = terrain.getReadyChunks();
 */
class TerrainGenerator {
  public:
    TerrainGenerator() = default;
    ~TerrainGenerator();

    // Non-copyable
    TerrainGenerator(const TerrainGenerator &) = delete;
    TerrainGenerator &operator=(const TerrainGenerator &) = delete;

    // Lifecycle
    void init(const TerrainConfig &config);
    void shutdown();

    // Heightmap management
    void setHeightmap(std::shared_ptr<Texture> heightmap);
    void generateHeightmap();
    bool hasHeightmap() const { return !m_heightmapData.empty() || m_heightmapTexture != nullptr; }

    // CPU height sampling (for physics, object placement)
    float sampleHeight(float worldX, float worldZ) const;
    glm::vec3 sampleNormal(float worldX, float worldZ) const;
    bool isInBounds(float worldX, float worldZ) const;

    // Chunk management
    void loadChunk(glm::ivec2 coord);
    void unloadChunk(glm::ivec2 coord);
    void loadChunksAroundPosition(const glm::vec3 &position, int32_t radius);

    // Mark chunk as needing regeneration
    void markChunkDirty(glm::ivec2 coord);

    // Per-frame update (LOD selection, regenerate dirty chunks)
    void update(const glm::vec3 &cameraPos);

    // Queries
    TerrainChunk *getChunkAtWorld(float worldX, float worldZ);
    TerrainChunk *getChunkAtCoord(glm::ivec2 coord);
    const TerrainChunk *getChunkAtCoord(glm::ivec2 coord) const;
    std::vector<TerrainChunk *> getReadyChunks();

    // Coordinate conversion
    glm::ivec2 worldToChunkCoord(float worldX, float worldZ) const;
    glm::vec2 chunkCoordToWorldCenter(glm::ivec2 coord) const;

    // Accessors
    const TerrainConfig &getConfig() const { return m_config; }
    size_t getLoadedChunkCount() const { return m_chunks.size(); }
    const ChunkGrid &getChunkGrid() const { return m_chunks; }
    std::shared_ptr<Texture> getHeightmapTexture() const { return m_heightmapTexture; }

    // Debug
    void setWireframe(bool enabled) { m_wireframe = enabled; }
    bool isWireframe() const { return m_wireframe; }

  private:
    // Compute shader generation
    void initComputePipeline();
    void generateChunkGeometry(TerrainChunk &chunk);
    void createChunkBuffers(TerrainChunk &chunk);

    // Height sampling helpers
    float sampleHeightmapBilinear(float u, float v) const;
    glm::vec2 worldToHeightmapUV(float worldX, float worldZ) const;

    TerrainConfig m_config;
    ChunkGrid m_chunks;

    // Heightmap data (CPU-side for sampling)
    std::vector<float> m_heightmapData;
    uint32_t m_heightmapWidth = 0;
    uint32_t m_heightmapHeight = 0;

    // GPU heightmap texture (for compute shader sampling)
    std::shared_ptr<Texture> m_heightmapTexture;

    // Compute shader resources
    std::shared_ptr<Shader> m_computeShader;
    std::shared_ptr<ComputePipeline> m_computePipeline;
    std::shared_ptr<CommandPool> m_commandPool;
    std::shared_ptr<CommandBuffer> m_commandBuffer;

    bool m_initialized = false;
    bool m_wireframe = false;
};

} // namespace Rapture

#endif // RAPTURE__TERRAIN_GENERATOR_H

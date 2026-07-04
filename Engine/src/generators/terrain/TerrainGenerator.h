#ifndef RAPTURE__TERRAIN_GENERATOR_H
#define RAPTURE__TERRAIN_GENERATOR_H

#include "TerrainCuller.h"
#include "TerrainTypes.h"

#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/command_buffers/CommandPool.h"
#include "buffers/IndexBuffer.h"
#include "buffers/StorageBuffer.h"
#include "generators/textures/ProceduralTextures.h"
#include "materials/MaterialInstance.h"
#include "pipelines/ComputePipeline.h"
#include "shaders/Shader.h"
#include "textures/Texture.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace Rapture {

class Frustum;

/**
 * @brief GPU-driven terrain system.
 *
 * All chunk data is computed on GPU each frame based on camera position.
 * CPU only allocates buffers and dispatches compute shaders.
 */
class TerrainGenerator {
  public:
    TerrainGenerator() = default;
    ~TerrainGenerator();

    // Lifecycle
    void init(const TerrainConfig &config);
    void shutdown();

    // Noise configuration
    void setNoiseTexture(TerrainNoiseCategory category, AssetPtr<Texture> texture);
    Texture *getNoiseTexture(TerrainNoiseCategory category) const;
    MultiNoiseConfig &getMultiNoiseConfig() { return m_multiNoiseConfig; }
    const MultiNoiseConfig &getMultiNoiseConfig() const { return m_multiNoiseConfig; }
    void bakeSplineCurves();
    Texture *getSplineCurveTexture() const { return m_splineCurveTexture.get(); }
    void generateDefaultNoiseTextures();

    void setSingleHeightmap(AssetPtr<Texture> texture) { m_noiseTextures[CONTINENTALNESS] = std::move(texture); }
    Texture *getSingleHeightmap() const { return m_noiseTextures[CONTINENTALNESS].get(); }

    // Per-frame update: computes chunk data on GPU, runs culling
    void update(const glm::vec3 &cameraPos, Frustum &frustum, uint32_t frameIndex);

    // Rendering resources
    std::shared_ptr<StorageBuffer> getChunkDataBuffer() const { return m_chunkDataBuffer; }
    VkBuffer getIndexBuffer(uint32_t lod) const;
    uint32_t getIndexCount(uint32_t lod) const { return getTerrainLODIndexCount(lod); }

    TerrainCuller *getTerrainCuller() { return m_culler.get(); }
    TerrainCullBuffers *getCullBuffers(uint32_t frameIndex)
    {
        return frameIndex < m_cullBuffers.size() ? &m_cullBuffers[frameIndex] : nullptr;
    }

    // Accessors
    const TerrainConfig &getConfig() const { return m_config; }
    TerrainConfig &getConfigMutable() { return m_config; }
    uint32_t getChunkCount() const { return m_chunkCount; }
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
    void initComputePipeline();

    // GPU compute: generates chunk grid around camera, computes bounds
    void dispatchChunkUpdate(const glm::vec3 &cameraPos);

    TerrainConfig m_config;
    uint32_t m_chunkCount = 0;

    MultiNoiseConfig m_multiNoiseConfig;
    AssetPtr<Texture> m_noiseTextures[TERRAIN_NC_COUNT];
    std::unique_ptr<Texture> m_splineCurveTexture;

    // Shared index buffers (one per LOD, grid topology)
    std::shared_ptr<IndexBuffer> m_indexBuffers[TERRAIN_LOD_COUNT];

    std::shared_ptr<StorageBuffer> m_chunkDataBuffer;

    std::unique_ptr<TerrainCuller> m_culler;
    std::vector<TerrainCullBuffers> m_cullBuffers;

    Shader *m_chunkComputeShader;
    std::shared_ptr<ComputePipeline> m_chunkComputePipeline;
    CommandPoolHash m_computePoolHash = 0;

    bool m_initialized = false;
    bool m_wireframe = false;

    std::vector<AssetRef> m_shaderAssets;

    std::shared_ptr<MaterialInstance> m_grassMaterial;
    std::shared_ptr<MaterialInstance> m_rockMaterial;
    std::shared_ptr<MaterialInstance> m_snowMaterial;
};

} // namespace Rapture

#endif // RAPTURE__TERRAIN_GENERATOR_H

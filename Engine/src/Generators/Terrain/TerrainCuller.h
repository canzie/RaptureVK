#ifndef RAPTURE__TERRAIN_CULLER_H
#define RAPTURE__TERRAIN_CULLER_H

#include "TerrainTypes.h"

#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/StorageBuffers/StorageBuffer.h"
#include "Pipelines/ComputePipeline.h"
#include "Shaders/Shader.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace Rapture {

class Frustum;

struct TerrainCullBuffers {
    std::vector<std::unique_ptr<StorageBuffer>> indirectBuffers;
    std::unique_ptr<StorageBuffer> drawCountBuffer;
    std::vector<uint32_t> indirectCapacities;
    std::vector<uint32_t> processedLODs;
};

class TerrainCuller {
  public:
    TerrainCuller(std::shared_ptr<StorageBuffer> chunkDataBuffer, uint32_t chunkCount, float heightScale,
                  uint32_t initialIndirectCapacity, VmaAllocator allocator);
    ~TerrainCuller();

    TerrainCuller(const TerrainCuller &) = delete;
    TerrainCuller &operator=(const TerrainCuller &) = delete;

    TerrainCullBuffers createBuffers(const std::vector<uint32_t> &lodsToProcess);

    void runCull(TerrainCullBuffers &buffers, uint32_t frustumBindlessIndex, const glm::vec3 &cullOrigin);

    void setChunkCount(uint32_t count) { m_chunkCount = count; }

  private:
    void initCullPipeline();

    std::shared_ptr<StorageBuffer> m_chunkDataBuffer;
    uint32_t m_chunkCount;
    float m_heightScale;
    uint32_t m_initialIndirectCapacity;
    VmaAllocator m_allocator;

    std::shared_ptr<Shader> m_cullShader;
    std::shared_ptr<ComputePipeline> m_cullPipeline;
    CommandPoolHash m_commandPoolHash;
};

} // namespace Rapture

#endif // RAPTURE__TERRAIN_CULLER_H

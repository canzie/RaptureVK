#pragma once

#include "Buffers/BufferPool.h"
#include "Buffers/StorageBuffers/StorageBuffer.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace Rapture {

class Mesh;

// Holds the data that will be indexed using the draw index in the shaders
// currently bindless indices
struct ObjectInfo {
    uint32_t meshIndex;
    uint32_t materialIndex;
};

class MDIBatch {
  public:
    MDIBatch(std::shared_ptr<BufferAllocation> vboArena, std::shared_ptr<BufferAllocation> iboArena, BufferLayout &bufferLayout,
             VkIndexType indexType);
    ~MDIBatch();

    void addObject(const Mesh &mesh, uint32_t meshIndex, uint32_t materialIndex);

    // commit the data to the gpu buffers
    // should be called at the end, when all of the objects have been added
    void uploadBuffers();

    // clear the cpu data
    void clear();

    std::shared_ptr<StorageBuffer> getIndirectBuffer();
    std::shared_ptr<StorageBuffer> getBatchInfoBuffer();
    uint32_t getBatchInfoBufferIndex() const;
    uint32_t getVboArenaId() const { return m_vboArenaId; }
    uint32_t getIboArenaId() const { return m_iboArenaId; }
    uint32_t getDrawCount() const { return m_cpuIndirectCommands.size(); }
    uint32_t getAllocatedSize() const { return m_allocatedSize; }

    BufferLayout &getBufferLayout() const { return m_bufferLayout; }
    VkBuffer getVertexBuffer() const { return m_vertexBuffer; }
    VkBuffer getIndexBuffer() const { return m_indexBuffer; }
    VkIndexType getIndexType() const { return m_indexType; }

  private:
    std::shared_ptr<StorageBuffer> m_indirectBuffer;
    std::shared_ptr<StorageBuffer> m_batchInfoBuffer;

    std::vector<VkDrawIndexedIndirectCommand> m_cpuIndirectCommands;
    std::vector<ObjectInfo> m_cpuObjectInfo;

    uint32_t m_vboArenaId;
    uint32_t m_iboArenaId;

    uint32_t m_allocatedSize = 0;
    uint32_t m_batchInfoBufferIndex = UINT32_MAX; // init at max to avoid confusion with the first element
    bool m_buffersCreated = false;

    // needs to be used for the final draw commands
    // easier to store a copy here than to always take the ones from the first element
    // this is just cleaner
    BufferLayout &m_bufferLayout;
    VkBuffer m_vertexBuffer;
    VkBuffer m_indexBuffer;
    VkIndexType m_indexType;
};

// when a render pass uses multiple batches we need a way to neatly organise this
class MDIBatchMap {
  public:
    MDIBatchMap() = default;

    // called at the start of the frame
    // this is used to clear the batch map
    void beginFrame();

    // obtain = create or get
    MDIBatch *obtainBatch(std::shared_ptr<BufferAllocation> vboArena, std::shared_ptr<BufferAllocation> iboArena,
                          BufferLayout &bufferLayout, VkIndexType indexType);

    const std::unordered_map<uint64_t, std::unique_ptr<MDIBatch>> &getBatches() const { return m_batches; }

  private:
    // we take the buffer ids from both buffers, then add these numbers to generate a unique key
    // e.g. 123 + 456 = 123456
    std::unordered_map<uint64_t, std::unique_ptr<MDIBatch>> m_batches;
};

} // namespace Rapture

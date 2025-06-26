#pragma once

#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Buffers/StorageBuffers/StorageBuffer.h"
#include "Buffers/BufferPool.h"

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace Rapture {

class Mesh;

struct ObjectInfo {
    uint32_t meshIndex;
    uint32_t materialIndex;
};

class MDIBatch {
public:
    MDIBatch(std::shared_ptr<BufferAllocation> vboArena, std::shared_ptr<BufferAllocation> iboArena, BufferLayout& bufferLayout, VkIndexType indexType);
    ~MDIBatch();

    void addObject(const Mesh& mesh, uint32_t meshIndex, uint32_t materialIndex);

    void uploadBuffers();

    void clear();

    std::shared_ptr<StorageBuffer> getIndirectBuffer();
    std::shared_ptr<StorageBuffer> getBatchInfoBuffer();
    uint32_t getBatchInfoBufferIndex() const;
    uint32_t getVboArenaId() const { return m_vboArenaId; }
    uint32_t getIboArenaId() const { return m_iboArenaId; }
    uint32_t getDrawCount() const { return m_cpuIndirectCommands.size(); }

    BufferLayout& getBufferLayout() const { return m_bufferLayout; }
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
    uint32_t m_batchInfoBufferIndex = UINT32_MAX;
    bool m_buffersCreated = false;

    BufferLayout& m_bufferLayout;
    VkBuffer m_vertexBuffer;
    VkBuffer m_indexBuffer;
    VkIndexType m_indexType;


};

class MDIBatchMap {
public:
    MDIBatchMap();

    void beginFrame();
    
    MDIBatch* obtainBatch(std::shared_ptr<BufferAllocation> vboArena, std::shared_ptr<BufferAllocation> iboArena, BufferLayout& bufferLayout, VkIndexType indexType);

    const std::unordered_map<uint64_t, std::unique_ptr<MDIBatch>>& getBatches() const { return m_batches; }

private:
    std::unordered_map<uint64_t, std::unique_ptr<MDIBatch>> m_batches;
};

}



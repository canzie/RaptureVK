#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "buffers/BufferLayout.h"
#include "buffers/IndexBuffer.h"
#include "buffers/VertexBuffer.h"

#include "buffers/BufferPool.h"

namespace Rapture {

struct MeshAllocatorParams {
    void *vertexData = nullptr;
    uint32_t vertexDataSize = 0;
    void *indexData = nullptr;
    uint32_t indexDataSize = 0;
    uint32_t indexCount = 0;
    uint32_t indexType = 0;

    BufferLayout bufferLayout;

    /**
     * @brief Serializes this mesh data into a self-contained blob of header, vertex and index bytes
     * @return The serialized bytes
     */
    std::vector<uint8_t> serialize() const;
};

class Mesh {

  public:
    Mesh(MeshAllocatorParams &params);
    Mesh();
    ~Mesh();

    void setMeshData(MeshAllocatorParams &params);

    /**
     * @brief Builds a mesh from a blob produced by MeshAllocatorParams::serialize
     * @param blob The serialized mesh bytes
     * @return The mesh, or nullptr if the blob is invalid
     */
    static std::unique_ptr<Mesh> deserialize(std::span<const uint8_t> blob);

    std::shared_ptr<VertexBuffer> getVertexBuffer() const { return m_vertexBuffer; }
    std::shared_ptr<IndexBuffer> getIndexBuffer() const { return m_indexBuffer; }

    uint32_t getIndexCount() const { return m_indexCount; }

    // static std::shared_ptr<UniformBuffer> createBindlessMeshDataBuffer();

    std::shared_ptr<BufferAllocation> getIndexAllocation() { return m_indexAllocation; }
    std::shared_ptr<BufferAllocation> getVertexAllocation() { return m_vertexAllocation; }

    std::shared_ptr<BufferAllocation> getIndexAllocation() const { return m_indexAllocation; }
    std::shared_ptr<BufferAllocation> getVertexAllocation() const { return m_vertexAllocation; }

    /**
     * @brief Approximate GPU footprint of this mesh in bytes
     * @return The combined size of the vertex and index allocations
     */
    uint64_t getSizeBytes() const;

  private:
    uint32_t m_indexCount;
    std::shared_ptr<VertexBuffer> m_vertexBuffer;
    std::shared_ptr<IndexBuffer> m_indexBuffer;

    std::shared_ptr<BufferAllocation> m_indexAllocation;
    std::shared_ptr<BufferAllocation> m_vertexAllocation;

    // std::shared_ptr<UniformBuffer> m_objectDataBuffer; // per mesh data
    // uint32_t m_bindlessMeshDataIndex;
    // static std::unique_ptr<DescriptorSubAllocationBase<Buffer>> s_bindlessMeshDataAllocation;
};

} // namespace Rapture

#ifndef RAPTURE__MESH_H
#define RAPTURE__MESH_H

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "assets/asset_manager/AssetCommon.h"

#include <glm/glm.hpp>

#include "gpu/buffers/BufferLayout.h"
#include "gpu/buffers/IndexBuffer.h"
#include "gpu/buffers/VertexBuffer.h"

#include "gpu/buffers/BufferPool.h"

namespace Rapture {

class BLAS;

struct MeshAllocatorParams {
    void *vertexData = nullptr;
    uint32_t vertexDataSize = 0;
    void *indexData = nullptr;
    uint32_t indexDataSize = 0;
    uint32_t indexCount = 0;
    VkIndexType indexType = VK_INDEX_TYPE_UINT16;

    glm::vec3 boundsMin = glm::vec3(0.0f);
    glm::vec3 boundsMax = glm::vec3(0.0f);

    BufferLayout bufferLayout;

    /**
     * @brief Serializes this mesh data into a self-contained blob of header, vertex and index bytes
     * @return The serialized bytes
     */
    std::vector<uint8_t> serialize() const;

    /**
     * @brief Reads back what serialize wrote
     * @param blob The serialized bytes, which have to outlive the params read from them
     * @param params Filled in from the blob, with its vertex and index data aimed into it
     * @return True if the blob was read
     */
    static bool deserialize(std::span<const uint8_t> blob, MeshAllocatorParams &params);
};

class Mesh {

  public:
    Mesh(MeshAllocatorParams &params);
    Mesh();
    virtual ~Mesh();

    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;
    Mesh(Mesh &&other) noexcept;
    Mesh &operator=(Mesh &&other) noexcept;

    /**
     * @brief Converts a glTF index accessor's component type to the index type meshes carry
     * @param componentType The glTF componentType of one index
     * @return The matching index type, or VK_INDEX_TYPE_UINT16 for a component type indices cannot use
     */
    static VkIndexType indexTypeFromComponentType(uint32_t componentType);

    void setMeshData(MeshAllocatorParams &params);

    std::shared_ptr<VertexBuffer> getVertexBuffer() const { return m_vertexBuffer; }
    std::shared_ptr<IndexBuffer> getIndexBuffer() const { return m_indexBuffer; }

    uint32_t getIndexCount() const { return m_indexCount; }

    const glm::vec3 &getBoundsMin() const { return m_boundsMin; }
    const glm::vec3 &getBoundsMax() const { return m_boundsMax; }

    /**
     * @brief Sets the extents of this mesh's geometry, which are authored rather than computed
     * @param min Corner of the box with the smallest coordinates
     * @param max Corner of the box with the largest coordinates
     */
    void setBounds(const glm::vec3 &min, const glm::vec3 &max);

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

    /**
     * @brief Asks for this mesh's acceleration structure, which is ready some time after this returns
     * @param assetHandle The asset this mesh belongs to, referenced until the structure is ready
     * @return True if the structure was created and handed over to be built
     */
    bool requestBLAS(AssetHandle assetHandle);

    /**
     * @brief Get this mesh's acceleration structure without asking for one
     * @return The acceleration structure, or nullptr if requestBLAS has not succeeded
     */
    BLAS *getBLAS() const { return m_blas.get(); }

    /**
     * @brief Serializes this geometry by reading it back off the GPU
     * @return A geometry blob, empty if this mesh holds none
     */
    std::vector<uint8_t> serializeGeometry() const;

  private:
    uint32_t m_indexCount;
    glm::vec3 m_boundsMin = glm::vec3(0.0f);
    glm::vec3 m_boundsMax = glm::vec3(0.0f);
    std::shared_ptr<VertexBuffer> m_vertexBuffer;
    std::shared_ptr<IndexBuffer> m_indexBuffer;

    std::shared_ptr<BufferAllocation> m_indexAllocation;
    std::shared_ptr<BufferAllocation> m_vertexAllocation;

    std::unique_ptr<BLAS> m_blas;

    // std::shared_ptr<UniformBuffer> m_objectDataBuffer; // per mesh data
    // uint32_t m_bindlessMeshDataIndex;
    // static std::unique_ptr<DescriptorSubAllocationBase<Buffer>> s_bindlessMeshDataAllocation;
};

} // namespace Rapture

#endif // RAPTURE__MESH_H

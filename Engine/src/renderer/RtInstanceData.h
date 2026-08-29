#ifndef RAPTURE__RT_INSTANCE_DATA_H
#define RAPTURE__RT_INSTANCE_DATA_H

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "gpu/buffers/StorageBuffer.h"
#include "gpu/vulkan_context/RenderContext.h"
#include "scene/Scene.h"

namespace Rapture {

class MaterialInstance;

/**
 * @brief One run of a traced mesh, reached by the geometry index the acceleration structure reports
 */
struct RtGeometryInfo {
    alignas(4) uint32_t materialIndex; ///< bindless index into the material header SSBO
    alignas(4) uint32_t firstIndex;    ///< where the run starts in its mesh's index data
};

struct RtInstanceInfo {
    alignas(4) uint32_t firstGeometry; // where this instance's runs start in the geometry info SSBO

    alignas(4) uint32_t iboIndex; // index of the buffer in the bindless buffers array
    alignas(4) uint32_t vboIndex; // index of the buffer in the bindless buffers array

    alignas(16) glm::mat4 modelMatrix;

    alignas(4) uint32_t positionAttributeOffsetBytes; // Offset of position *within* the stride
    alignas(4) uint32_t texCoordAttributeOffsetBytes;
    alignas(4) uint32_t normalAttributeOffsetBytes;
    alignas(4) uint32_t tangentAttributeOffsetBytes;

    alignas(4) uint32_t vertexStrideBytes; // Stride of the vertex buffer in bytes
    alignas(4) uint32_t indexType;
};

class RtInstanceData {
  public:
    RtInstanceData(const RenderContext &renderContext);
    ~RtInstanceData();

    void update(Scene &scene);

    std::shared_ptr<StorageBuffer> getBuffer() { return m_buffer; }
    uint32_t getInstanceCount() const { return m_instanceCount; }

    std::shared_ptr<StorageBuffer> getGeometryBuffer() { return m_geometryBuffer; }

    void markMaterialDirty(MaterialInstance *material);

  private:
    void rebuild(Scene &scene);
    void patchDirty(Scene &scene);

    RenderContext m_rc;

    std::shared_ptr<StorageBuffer> m_buffer;
    std::shared_ptr<StorageBuffer> m_geometryBuffer;
    uint32_t m_instanceCount = 0;
    VmaAllocator m_allocator;

    std::unordered_set<MaterialInstance *> m_dirtyMaterials;

    // one buffer, so one position in the transform channel
    ecs::Bookmark m_transformBookmark;

    std::unordered_map<MaterialInstance *, std::vector<uint32_t>> m_materialToOffsets;
    std::unordered_map<uint32_t, uint32_t> m_entityToOffset;

    uint64_t m_tlasRevision = 0;

    uint32_t m_meshDataSSBOIndex = UINT32_MAX;
    uint32_t m_geometryDataSSBOIndex = UINT32_MAX;
};

} // namespace Rapture

#endif // RAPTURE__RT_INSTANCE_DATA_H

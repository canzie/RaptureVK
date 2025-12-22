#ifndef RAPTURE__RT_INSTANCE_DATA_H
#define RAPTURE__RT_INSTANCE_DATA_H

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "Buffers/StorageBuffers/StorageBuffer.h"
#include "Scenes/Scene.h"

namespace Rapture {

class MaterialInstance;

struct RtInstanceInfo {
    alignas(4) uint32_t AlbedoTextureIndex;
    alignas(4) uint32_t NormalTextureIndex;

    alignas(16) glm::vec3 albedo;

    alignas(16) glm::vec3 emissiveColor;
    alignas(4) uint32_t EmissiveFactorTextureIndex;

    alignas(4) uint32_t iboIndex; // index of the buffer in the bindless buffers array
    alignas(4) uint32_t vboIndex; // index of the buffer in the bindless buffers array

    alignas(4)
        uint32_t meshIndex; // index of the mesh in the mesh array, this is the same index as the tlasinstance instanceCustomIndex

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
    RtInstanceData();
    ~RtInstanceData();

    void update(std::shared_ptr<Scene> scene);

    std::shared_ptr<StorageBuffer> getBuffer() { return m_buffer; }
    uint32_t getInstanceCount() const { return m_instanceCount; }

    void markMaterialDirty(MaterialInstance *material);
    void markTransformDirty(uint32_t entityID);

  private:
    void rebuild(std::shared_ptr<Scene> scene);
    void patchDirty(std::shared_ptr<Scene> scene);

    std::shared_ptr<StorageBuffer> m_buffer;
    uint32_t m_instanceCount = 0;
    VmaAllocator m_allocator;

    std::unordered_set<MaterialInstance *> m_dirtyMaterials;
    std::unordered_set<uint32_t> m_dirtyTransforms;

    std::unordered_map<MaterialInstance *, std::vector<uint32_t>> m_materialToOffsets;
    std::unordered_map<uint32_t, uint32_t> m_entityToOffset;

    uint32_t m_lastTlasInstanceCount = 0;

    uint32_t m_meshDataSSBOIndex = UINT32_MAX;
};

} // namespace Rapture

#endif // RAPTURE__RT_INSTANCE_DATA_H

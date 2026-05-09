#include "MeshDataBuffer.h"
#include "Components/Components.h"
#include "Buffers/Descriptors/DescriptorSet.h"

namespace Rapture {

MeshDataBuffer::MeshDataBuffer(uint32_t frameCount)
    : ObjectDataBuffer(DescriptorSetBindingLocation::MESH_DATA_UBO, sizeof(MeshObjectData), frameCount)
    , m_lastTransformGenerations(frameCount, 0)
{
}

void MeshDataBuffer::onUpdate(const TransformComponent& transform, uint32_t flags, uint32_t frameIndex) {
    generation_t gen = transform.getGeneration();
    if (gen == m_lastTransformGenerations[frameIndex]) {
        transformChanged = false;
        return;
    }
    m_lastTransformGenerations[frameIndex] = gen;
    transformChanged = true;

    MeshObjectData data{};
    data.modelMatrix = transform.transformMatrix();
    data.flags = flags;

    updateBuffer(&data, sizeof(MeshObjectData), frameIndex);
}

} 
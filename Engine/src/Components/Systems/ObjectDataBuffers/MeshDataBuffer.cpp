#include "MeshDataBuffer.h"
#include "Components/Components.h"
#include "Buffers/Descriptors/DescriptorSet.h"

namespace Rapture {

MeshDataBuffer::MeshDataBuffer(uint32_t frameCount) 
    : ObjectDataBuffer(DescriptorSetBindingLocation::MESH_DATA_UBO, sizeof(MeshObjectData), frameCount) {
}

void MeshDataBuffer::update(const TransformComponent& transform, uint32_t flags, uint32_t frameIndex) {
    MeshObjectData data{};
    data.modelMatrix = transform.transformMatrix();
    data.flags = flags;

    updateBuffer(&data, sizeof(MeshObjectData), frameIndex);
}

} 
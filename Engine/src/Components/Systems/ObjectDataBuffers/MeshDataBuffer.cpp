#include "MeshDataBuffer.h"
#include "Components/Components.h"
#include "Buffers/Descriptors/DescriptorSet.h"

namespace Rapture {

MeshDataBuffer::MeshDataBuffer() 
    : ObjectDataBuffer(DescriptorSetBindingLocation::MESH_DATA_UBO, sizeof(MeshObjectData)) {
}

void MeshDataBuffer::update(const TransformComponent& transform, uint32_t flags) {
    MeshObjectData data{};
    data.modelMatrix = transform.transformMatrix();
    data.flags = flags;

    updateBuffer(&data, sizeof(MeshObjectData));
}

} 
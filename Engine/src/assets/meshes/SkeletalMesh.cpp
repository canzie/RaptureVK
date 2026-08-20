#include "SkeletalMesh.h"

#include "core/utils/Log.h"

namespace Rapture {

SkeletalMesh::SkeletalMesh(MeshAllocatorParams &params, AssetHandle skeleton, std::vector<glm::mat4> inverseBindMatrices)
    : Mesh(params), m_skeleton(skeleton), m_inverseBindMatrices(std::move(inverseBindMatrices))
{
    const BufferLayout &layout = params.bufferLayout;
    if (layout.getAttributeOffset(BufferAttributeID::JOINTS_0) == UINT32_MAX ||
        layout.getAttributeOffset(BufferAttributeID::WEIGHTS_0) == UINT32_MAX) {
        RP_CORE_ERROR("skeletal mesh has no joints or weights to deform with");
    }

    if (m_skeleton == INVALID_ASSET_HANDLE) {
        RP_CORE_ERROR("skeletal mesh is bound to no skeleton");
    }
}

} // namespace Rapture

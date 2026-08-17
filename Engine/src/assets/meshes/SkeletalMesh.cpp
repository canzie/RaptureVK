#include "SkeletalMesh.h"

#include "core/utils/Log.h"

#include <cstring>

namespace Rapture {

static constexpr uint32_t SKELETAL_MESH_MAGIC = Asset_fourCC("SKMH");

// Major in the high 16 bits, minor in the low 16. A backward-compatible change bumps minor, a
// breaking one bumps major. Readers reject a different major and warn on a different minor.
static constexpr uint16_t SKELETAL_MESH_VERSION_MAJOR = 1;
static constexpr uint16_t SKELETAL_MESH_VERSION_MINOR = 0;
static constexpr uint32_t SKELETAL_MESH_VERSION =
    (static_cast<uint32_t>(SKELETAL_MESH_VERSION_MAJOR) << 16) | SKELETAL_MESH_VERSION_MINOR;

struct SkeletalMeshBlobHeader {
    uint32_t magic = SKELETAL_MESH_MAGIC;
    uint32_t version = SKELETAL_MESH_VERSION;
    uint64_t skeleton = INVALID_ASSET_HANDLE;
    uint32_t jointCount = 0;
    uint32_t inverseBindOffset = 0;
    uint32_t geometryOffset = 0;
    uint32_t reserved[1] = {}; // pad to 32 bytes, consume for backward-compatible additions
};

static_assert(sizeof(SkeletalMeshBlobHeader) == 32, "skeletal mesh blob header is a fixed 32-byte directory");

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

static std::vector<uint8_t> s_wrapGeometry(const std::vector<uint8_t> &geometry, AssetHandle skeleton,
                                           const std::vector<glm::mat4> &inverseBindMatrices)
{
    if (geometry.empty()) {
        return {};
    }

    SkeletalMeshBlobHeader header;
    header.skeleton = skeleton;
    header.jointCount = static_cast<uint32_t>(inverseBindMatrices.size());
    header.inverseBindOffset = sizeof(SkeletalMeshBlobHeader);
    header.geometryOffset = header.inverseBindOffset + header.jointCount * static_cast<uint32_t>(sizeof(glm::mat4));

    std::vector<uint8_t> blob(header.geometryOffset + geometry.size());
    std::memcpy(blob.data(), &header, sizeof(SkeletalMeshBlobHeader));

    if (header.jointCount > 0) {
        std::memcpy(blob.data() + header.inverseBindOffset, inverseBindMatrices.data(), header.jointCount * sizeof(glm::mat4));
    }
    std::memcpy(blob.data() + header.geometryOffset, geometry.data(), geometry.size());

    return blob;
}

std::vector<uint8_t> SkeletalMesh::serialize() const
{
    return s_wrapGeometry(serializeGeometry(), m_skeleton, m_inverseBindMatrices);
}

std::vector<uint8_t> SkeletalMesh::serializeParams(const MeshAllocatorParams &params, AssetHandle skeleton,
                                                   const std::vector<glm::mat4> &inverseBindMatrices)
{
    return s_wrapGeometry(params.serialize(), skeleton, inverseBindMatrices);
}

std::unique_ptr<SkeletalMesh> SkeletalMesh::deserialize(std::span<const uint8_t> blob)
{
    if (blob.size() < sizeof(SkeletalMeshBlobHeader)) {
        RP_CORE_ERROR("skeletal mesh blob is smaller than its header");
        return nullptr;
    }

    SkeletalMeshBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(SkeletalMeshBlobHeader));

    if (header.magic != SKELETAL_MESH_MAGIC) {
        RP_CORE_ERROR("blob is not a skeletal mesh");
        return nullptr;
    }

    uint16_t major = static_cast<uint16_t>(header.version >> 16);
    if (major != SKELETAL_MESH_VERSION_MAJOR) {
        RP_CORE_ERROR("skeletal mesh blob major version {} is incompatible with {}", major, SKELETAL_MESH_VERSION_MAJOR);
        return nullptr;
    }
    if (header.version != SKELETAL_MESH_VERSION) {
        RP_CORE_WARN("skeletal mesh blob minor version differs from {}, reading the known header fields",
                     SKELETAL_MESH_VERSION_MINOR);
    }

    if (header.geometryOffset > blob.size() || header.inverseBindOffset + header.jointCount * sizeof(glm::mat4) > blob.size()) {
        RP_CORE_ERROR("skeletal mesh blob sections are out of range");
        return nullptr;
    }

    std::vector<glm::mat4> inverseBindMatrices(header.jointCount);
    if (header.jointCount > 0) {
        std::memcpy(inverseBindMatrices.data(), blob.data() + header.inverseBindOffset, header.jointCount * sizeof(glm::mat4));
    }

    MeshAllocatorParams params;
    if (!MeshAllocatorParams::deserialize(blob.subspan(header.geometryOffset), params)) {
        return nullptr;
    }

    return std::make_unique<SkeletalMesh>(params, header.skeleton, std::move(inverseBindMatrices));
}

} // namespace Rapture

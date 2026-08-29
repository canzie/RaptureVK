#include "ASkeletalMesh.h"

#include "core/utils/Log.h"

#include <cstring>

namespace Rapture {

const TypeInfo &ASkeletalMesh::staticType()
{
    static const TypeInfo type("ASkeletalMesh", &AMesh::staticType());
    return type;
}

const TypeInfo &ASkeletalMesh::type() const
{
    return staticType();
}

static constexpr uint32_t SKELETAL_MESH_MAGIC = Asset_fourCC("SKMH");

static constexpr uint16_t SKELETAL_MESH_VERSION_MAJOR = 3;
static constexpr uint16_t SKELETAL_MESH_VERSION_MINOR = 0;
static constexpr uint32_t SKELETAL_MESH_VERSION =
    (static_cast<uint32_t>(SKELETAL_MESH_VERSION_MAJOR) << 16) | SKELETAL_MESH_VERSION_MINOR;

struct ASkeletalMeshBlobHeader {
    uint32_t magic = SKELETAL_MESH_MAGIC;
    uint32_t version = SKELETAL_MESH_VERSION;
    uint64_t skeleton = INVALID_ASSET_HANDLE;
    uint32_t materialSlotCount = 0;
    uint32_t materialSlotOffset = 0;
    uint32_t jointCount = 0;
    uint32_t inverseBindOffset = 0;
    uint32_t geometryOffset = 0;
    uint32_t reserved[1] = {}; // pad to 40 bytes, consume for backward-compatible additions
};

static_assert(sizeof(ASkeletalMeshBlobHeader) == 40, "skeletal mesh asset blob header is a fixed 40-byte directory");

ASkeletalMesh::ASkeletalMesh(MeshAllocatorParams &params, AssetHandle skeleton, std::vector<glm::mat4> inverseBindMatrices,
                             std::vector<AssetHandle> materialSlots)
    : AMesh(std::move(materialSlots)), m_geometry(params, skeleton, std::move(inverseBindMatrices))
{
}

static std::vector<uint8_t> s_wrapGeometry(const std::vector<uint8_t> &geometry, AssetHandle skeleton,
                                           const std::vector<glm::mat4> &inverseBindMatrices,
                                           const std::vector<AssetHandle> &materialSlots)
{
    if (geometry.empty()) {
        return {};
    }

    ASkeletalMeshBlobHeader header;
    header.skeleton = skeleton;
    header.materialSlotCount = static_cast<uint32_t>(materialSlots.size());
    header.materialSlotOffset = sizeof(ASkeletalMeshBlobHeader);
    header.jointCount = static_cast<uint32_t>(inverseBindMatrices.size());
    header.inverseBindOffset = header.materialSlotOffset + header.materialSlotCount * static_cast<uint32_t>(sizeof(AssetHandle));
    header.geometryOffset = header.inverseBindOffset + header.jointCount * static_cast<uint32_t>(sizeof(glm::mat4));

    std::vector<uint8_t> blob(header.geometryOffset + geometry.size());
    std::memcpy(blob.data(), &header, sizeof(ASkeletalMeshBlobHeader));

    if (header.materialSlotCount > 0) {
        std::memcpy(blob.data() + header.materialSlotOffset, materialSlots.data(), materialSlots.size() * sizeof(AssetHandle));
    }
    if (header.jointCount > 0) {
        std::memcpy(blob.data() + header.inverseBindOffset, inverseBindMatrices.data(), header.jointCount * sizeof(glm::mat4));
    }
    std::memcpy(blob.data() + header.geometryOffset, geometry.data(), geometry.size());

    return blob;
}

std::vector<uint8_t> ASkeletalMesh::serialize() const
{
    return s_wrapGeometry(m_geometry.serializeGeometry(), m_geometry.getSkeleton(), m_geometry.getInverseBindMatrices(),
                          materialSlots());
}

std::vector<uint8_t> ASkeletalMesh::serializeParams(const MeshAllocatorParams &params, AssetHandle skeleton,
                                                    const std::vector<glm::mat4> &inverseBindMatrices,
                                                    const std::vector<AssetHandle> &materialSlots)
{
    return s_wrapGeometry(params.serialize(), skeleton, inverseBindMatrices, materialSlots);
}

std::unique_ptr<ASkeletalMesh> ASkeletalMesh::deserialize(std::span<const uint8_t> blob)
{
    if (blob.size() < sizeof(ASkeletalMeshBlobHeader)) {
        RP_CORE_ERROR("skeletal mesh blob is smaller than its header");
        return nullptr;
    }

    ASkeletalMeshBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(ASkeletalMeshBlobHeader));

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

    if (header.geometryOffset > blob.size() || header.inverseBindOffset + header.jointCount * sizeof(glm::mat4) > blob.size() ||
        header.materialSlotOffset + header.materialSlotCount * sizeof(AssetHandle) > blob.size()) {
        RP_CORE_ERROR("skeletal mesh blob sections are out of range");
        return nullptr;
    }

    std::vector<AssetHandle> materialSlots(header.materialSlotCount);
    if (header.materialSlotCount > 0) {
        std::memcpy(materialSlots.data(), blob.data() + header.materialSlotOffset,
                    header.materialSlotCount * sizeof(AssetHandle));
    }

    std::vector<glm::mat4> inverseBindMatrices(header.jointCount);
    if (header.jointCount > 0) {
        std::memcpy(inverseBindMatrices.data(), blob.data() + header.inverseBindOffset, header.jointCount * sizeof(glm::mat4));
    }

    MeshAllocatorParams params;
    if (!MeshAllocatorParams::deserialize(blob.subspan(header.geometryOffset), params)) {
        return nullptr;
    }

    return std::make_unique<ASkeletalMesh>(params, header.skeleton, std::move(inverseBindMatrices), std::move(materialSlots));
}

} // namespace Rapture

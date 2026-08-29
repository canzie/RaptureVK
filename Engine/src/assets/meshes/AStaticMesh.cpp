#include "AStaticMesh.h"

#include "core/utils/Log.h"

#include <cstring>

namespace Rapture {

const TypeInfo &AStaticMesh::staticType()
{
    static const TypeInfo type("AStaticMesh", &AMesh::staticType());
    return type;
}

const TypeInfo &AStaticMesh::type() const
{
    return staticType();
}

static constexpr uint32_t STATIC_MESH_MAGIC = Asset_fourCC("STMH");

static constexpr uint16_t STATIC_MESH_VERSION_MAJOR = 3;
static constexpr uint16_t STATIC_MESH_VERSION_MINOR = 0;
static constexpr uint32_t STATIC_MESH_VERSION =
    (static_cast<uint32_t>(STATIC_MESH_VERSION_MAJOR) << 16) | STATIC_MESH_VERSION_MINOR;

struct AStaticMeshBlobHeader {
    uint32_t magic = STATIC_MESH_MAGIC;
    uint32_t version = STATIC_MESH_VERSION;
    uint32_t materialSlotCount = 0;
    uint32_t materialSlotOffset = 0;
    uint32_t geometryOffset = 0;
    uint32_t reserved[3] = {}; // pad to 32 bytes, consume for backward-compatible additions
};

static_assert(sizeof(AStaticMeshBlobHeader) == 32, "static mesh asset blob header is a fixed 32-byte directory");

AStaticMesh::AStaticMesh(MeshAllocatorParams &params, std::vector<AssetHandle> materialSlots)
    : AMesh(std::move(materialSlots)), m_geometry(params)
{
}

AStaticMesh::AStaticMesh(StaticMesh geometry, std::vector<AssetHandle> materialSlots)
    : AMesh(std::move(materialSlots)), m_geometry(std::move(geometry))
{
}

static std::vector<uint8_t> s_wrapGeometry(const std::vector<uint8_t> &geometry, const std::vector<AssetHandle> &materialSlots)
{
    if (geometry.empty()) {
        return {};
    }

    AStaticMeshBlobHeader header;
    header.materialSlotCount = static_cast<uint32_t>(materialSlots.size());
    header.materialSlotOffset = sizeof(AStaticMeshBlobHeader);
    header.geometryOffset = header.materialSlotOffset + header.materialSlotCount * sizeof(AssetHandle);

    std::vector<uint8_t> blob(header.geometryOffset + geometry.size());
    std::memcpy(blob.data(), &header, sizeof(AStaticMeshBlobHeader));
    if (!materialSlots.empty()) {
        std::memcpy(blob.data() + header.materialSlotOffset, materialSlots.data(), materialSlots.size() * sizeof(AssetHandle));
    }
    std::memcpy(blob.data() + header.geometryOffset, geometry.data(), geometry.size());

    return blob;
}

std::vector<uint8_t> AStaticMesh::serialize() const
{
    return s_wrapGeometry(m_geometry.serializeGeometry(), materialSlots());
}

std::vector<uint8_t> AStaticMesh::serializeParams(const MeshAllocatorParams &params, const std::vector<AssetHandle> &materialSlots)
{
    return s_wrapGeometry(params.serialize(), materialSlots);
}

std::unique_ptr<AStaticMesh> AStaticMesh::deserialize(std::span<const uint8_t> blob)
{
    if (blob.size() < sizeof(AStaticMeshBlobHeader)) {
        RP_CORE_ERROR("static mesh blob is smaller than its header");
        return nullptr;
    }

    AStaticMeshBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(AStaticMeshBlobHeader));

    if (header.magic != STATIC_MESH_MAGIC) {
        RP_CORE_ERROR("blob is not a static mesh");
        return nullptr;
    }

    uint16_t major = static_cast<uint16_t>(header.version >> 16);
    if (major != STATIC_MESH_VERSION_MAJOR) {
        RP_CORE_ERROR("static mesh blob major version {} is incompatible with {}", major, STATIC_MESH_VERSION_MAJOR);
        return nullptr;
    }
    if (header.version != STATIC_MESH_VERSION) {
        RP_CORE_WARN("static mesh blob minor version differs from {}, reading the known header fields", STATIC_MESH_VERSION_MINOR);
    }

    if (header.geometryOffset > blob.size()) {
        RP_CORE_ERROR("static mesh blob geometry starts past its end");
        return nullptr;
    }

    size_t slotBytes = header.materialSlotCount * sizeof(AssetHandle);
    if (header.materialSlotOffset + slotBytes > blob.size()) {
        RP_CORE_ERROR("static mesh blob material slots are out of range");
        return nullptr;
    }

    std::vector<AssetHandle> materialSlots(header.materialSlotCount);
    if (header.materialSlotCount > 0) {
        std::memcpy(materialSlots.data(), blob.data() + header.materialSlotOffset, slotBytes);
    }

    MeshAllocatorParams params;
    if (!MeshAllocatorParams::deserialize(blob.subspan(header.geometryOffset), params)) {
        return nullptr;
    }

    return std::make_unique<AStaticMesh>(params, std::move(materialSlots));
}

} // namespace Rapture

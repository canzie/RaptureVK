#include "StaticMesh.h"

#include "assets/asset_manager/AssetCommon.h"
#include "core/utils/Log.h"

#include <cstring>

namespace Rapture {

static constexpr uint32_t STATIC_MESH_MAGIC = Asset_fourCC("STMH");

// Major in the high 16 bits, minor in the low 16. A backward-compatible change bumps minor, a
// breaking one bumps major. Readers reject a different major and warn on a different minor.
static constexpr uint16_t STATIC_MESH_VERSION_MAJOR = 1;
static constexpr uint16_t STATIC_MESH_VERSION_MINOR = 0;
static constexpr uint32_t STATIC_MESH_VERSION =
    (static_cast<uint32_t>(STATIC_MESH_VERSION_MAJOR) << 16) | STATIC_MESH_VERSION_MINOR;

struct StaticMeshBlobHeader {
    uint32_t magic = STATIC_MESH_MAGIC;
    uint32_t version = STATIC_MESH_VERSION;
    uint32_t geometryOffset = 0;
    uint32_t reserved[5] = {}; // pad to 32 bytes, consume for backward-compatible additions
};

static_assert(sizeof(StaticMeshBlobHeader) == 32, "static mesh blob header is a fixed 32-byte directory");

StaticMesh::StaticMesh(MeshAllocatorParams &params) : Mesh(params)
{
    for (const BufferAttribute &attrib : params.bufferLayout.buffer_attribs) {
        if (attrib.name == BufferAttributeID::JOINTS_0 || attrib.name == BufferAttributeID::WEIGHTS_0 ||
            attrib.name == BufferAttributeID::JOINTS_1 || attrib.name == BufferAttributeID::WEIGHTS_1) {
            RP_CORE_ERROR("static mesh carries {}, which only a skeletal mesh deforms with",
                          bufferAttributeIDToString(attrib.name));
        }
    }
}

static std::vector<uint8_t> s_wrapGeometry(const std::vector<uint8_t> &geometry)
{
    if (geometry.empty()) {
        return {};
    }

    StaticMeshBlobHeader header;
    header.geometryOffset = sizeof(StaticMeshBlobHeader);

    std::vector<uint8_t> blob(header.geometryOffset + geometry.size());
    std::memcpy(blob.data(), &header, sizeof(StaticMeshBlobHeader));
    std::memcpy(blob.data() + header.geometryOffset, geometry.data(), geometry.size());

    return blob;
}

std::vector<uint8_t> StaticMesh::serialize() const
{
    return s_wrapGeometry(serializeGeometry());
}

std::vector<uint8_t> StaticMesh::serializeParams(const MeshAllocatorParams &params)
{
    return s_wrapGeometry(params.serialize());
}

std::unique_ptr<StaticMesh> StaticMesh::deserialize(std::span<const uint8_t> blob)
{
    if (blob.size() < sizeof(StaticMeshBlobHeader)) {
        RP_CORE_ERROR("static mesh blob is smaller than its header");
        return nullptr;
    }

    StaticMeshBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(StaticMeshBlobHeader));

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

    MeshAllocatorParams params;
    if (!MeshAllocatorParams::deserialize(blob.subspan(header.geometryOffset), params)) {
        return nullptr;
    }

    return std::make_unique<StaticMesh>(params);
}

} // namespace Rapture

#include "Mesh.h"
#include "buffers/BufferPool.h"
#include "window_context/Application.h"

#include "logging/Log.h"

#include <cstring>

namespace Rapture {

static constexpr uint32_t MESH_BLOB_MAGIC = 0x484D5052; // "RPMH", identifies the blob as a mesh

// Version packs major in the high 16 bits and minor in the low 16. A backward-compatible change
// (consuming reserved header space) bumps minor, a breaking change (header grows, fields reorder)
// bumps major. Readers reject a different major and warn on a different minor.
static constexpr uint16_t MESH_BLOB_VERSION_MAJOR = 1;
static constexpr uint16_t MESH_BLOB_VERSION_MINOR = 0;
static constexpr uint32_t MESH_BLOB_VERSION = (static_cast<uint32_t>(MESH_BLOB_VERSION_MAJOR) << 16) | MESH_BLOB_VERSION_MINOR;

// Fixed 64-byte directory at the start of every mesh blob. The reserved tail absorbs new fields
// without moving the data, and the section offsets let readers jump straight to the bytes. When the
// reserved space runs out, grow the header and bump the major version.
struct MeshBlobHeader {
    uint32_t magic = MESH_BLOB_MAGIC;
    uint32_t version = MESH_BLOB_VERSION;
    uint32_t vertexDataSize = 0;
    uint32_t indexDataSize = 0;
    uint32_t indexCount = 0;
    uint32_t indexType = 0;
    uint32_t attribCount = 0;
    uint32_t isInterleaved = 0;
    uint32_t vertexSize = 0;
    uint32_t binding = 0;
    uint32_t flags = 0;
    uint32_t attribOffset = 0;     // byte offset of the attribute table
    uint32_t vertexDataOffset = 0; // byte offset of the vertex bytes
    uint32_t indexDataOffset = 0;  // byte offset of the index bytes
    uint32_t reserved[2] = {};     // pad to 64 bytes, consume for backward-compatible additions
};

static_assert(sizeof(MeshBlobHeader) == 64, "mesh blob header is a fixed 64-byte directory, bump to 128 and the major version if it must grow");

// std::unique_ptr<DescriptorSubAllocationBase<Buffer>> Mesh::s_bindlessMeshDataAllocation = nullptr;

Mesh::Mesh(MeshAllocatorParams &params)
{
    setMeshData(params);
}

Mesh::Mesh() : m_indexCount(0), m_vertexBuffer(nullptr), m_indexBuffer(nullptr) {}

Mesh::~Mesh()
{
    m_indexAllocation.reset();
    m_vertexAllocation.reset();
}

void Mesh::setMeshData(MeshAllocatorParams &params)
{
    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();

    m_indexCount = params.indexCount;

    BufferAllocationRequest vertexRequest;
    vertexRequest.size = params.vertexDataSize;
    vertexRequest.usage = BufferUsage::STATIC;
    vertexRequest.layout = params.bufferLayout;
    vertexRequest.indexSize = params.indexType == VK_INDEX_TYPE_UINT32 ? 4 : 2;
    vertexRequest.alignment = params.bufferLayout.calculateVertexSize();

    BufferAllocationRequest indexRequest;
    indexRequest.size = params.indexDataSize;
    indexRequest.usage = BufferUsage::STATIC;
    indexRequest.layout = params.bufferLayout;
    indexRequest.indexSize = params.indexType == VK_INDEX_TYPE_UINT32 ? 4 : 2;
    indexRequest.alignment = params.bufferLayout.calculateVertexSize();

    m_vertexBuffer = std::make_shared<VertexBuffer>(vertexRequest, vulkanContext.getVmaAllocator(), params.vertexData);
    m_indexBuffer = std::make_shared<IndexBuffer>(indexRequest, vulkanContext.getVmaAllocator(), params.indexData);

    m_indexAllocation = m_indexBuffer->getBufferAllocation();
    m_vertexAllocation = m_vertexBuffer->getBufferAllocation();

    if (!m_indexAllocation || !m_vertexAllocation) {
        RP_CORE_ERROR("Failed to create vertex or index buffer!");
        return;
    }
}

std::vector<uint8_t> MeshAllocatorParams::serialize() const
{
    uint32_t attribBytes = 0;
    for (const auto &attrib : bufferLayout.buffer_attribs) {
        attribBytes += sizeof(uint32_t) * 4 + static_cast<uint32_t>(attrib.type.size()); // name, componentType, offset, typeLen, chars
    }

    MeshBlobHeader header;
    header.vertexDataSize = vertexDataSize;
    header.indexDataSize = indexDataSize;
    header.indexCount = indexCount;
    header.indexType = indexType;
    header.attribCount = static_cast<uint32_t>(bufferLayout.buffer_attribs.size());
    header.isInterleaved = bufferLayout.isInterleaved ? 1u : 0u;
    header.vertexSize = bufferLayout.vertexSize;
    header.binding = bufferLayout.binding;
    header.flags = bufferLayout.flags;
    header.attribOffset = sizeof(MeshBlobHeader);
    header.vertexDataOffset = header.attribOffset + attribBytes;
    header.indexDataOffset = header.vertexDataOffset + vertexDataSize;

    std::vector<uint8_t> blob(header.indexDataOffset + indexDataSize);
    std::memcpy(blob.data(), &header, sizeof(MeshBlobHeader));

    size_t offset = header.attribOffset;
    auto writeU32 = [&](uint32_t v) {
        std::memcpy(blob.data() + offset, &v, sizeof(uint32_t));
        offset += sizeof(uint32_t);
    };
    for (const auto &attrib : bufferLayout.buffer_attribs) {
        writeU32(static_cast<uint32_t>(attrib.name));
        writeU32(attrib.componentType);
        writeU32(attrib.offset);
        writeU32(static_cast<uint32_t>(attrib.type.size()));
        std::memcpy(blob.data() + offset, attrib.type.data(), attrib.type.size());
        offset += attrib.type.size();
    }

    if (vertexData != nullptr && vertexDataSize > 0) {
        std::memcpy(blob.data() + header.vertexDataOffset, vertexData, vertexDataSize);
    }
    if (indexData != nullptr && indexDataSize > 0) {
        std::memcpy(blob.data() + header.indexDataOffset, indexData, indexDataSize);
    }

    return blob;
}

std::unique_ptr<Mesh> Mesh::deserialize(std::span<const uint8_t> blob)
{
    if (blob.size() < sizeof(MeshBlobHeader)) {
        RP_CORE_ERROR("Mesh blob is smaller than its header");
        return nullptr;
    }

    MeshBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(MeshBlobHeader));

    if (header.magic != MESH_BLOB_MAGIC) {
        RP_CORE_ERROR("Mesh blob has an invalid magic");
        return nullptr;
    }

    uint16_t major = static_cast<uint16_t>(header.version >> 16);
    if (major != MESH_BLOB_VERSION_MAJOR) {
        RP_CORE_ERROR("Mesh blob major version {} is incompatible with {}", major, MESH_BLOB_VERSION_MAJOR);
        return nullptr;
    }
    if (header.version != MESH_BLOB_VERSION) {
        RP_CORE_WARN("Mesh blob minor version differs from {}, reading the known header fields", MESH_BLOB_VERSION_MINOR);
    }

    if (header.vertexDataOffset + header.vertexDataSize > blob.size() ||
        header.indexDataOffset + header.indexDataSize > blob.size()) {
        RP_CORE_ERROR("Mesh blob data sections are out of range");
        return nullptr;
    }

    MeshAllocatorParams params;
    params.vertexDataSize = header.vertexDataSize;
    params.indexDataSize = header.indexDataSize;
    params.indexCount = header.indexCount;
    params.indexType = header.indexType;
    params.bufferLayout.isInterleaved = header.isInterleaved != 0;
    params.bufferLayout.vertexSize = header.vertexSize;
    params.bufferLayout.binding = header.binding;
    params.bufferLayout.flags = header.flags;

    size_t offset = header.attribOffset;
    auto readU32 = [&](uint32_t &out) -> bool {
        if (offset + sizeof(uint32_t) > blob.size()) {
            return false;
        }
        std::memcpy(&out, blob.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        return true;
    };

    params.bufferLayout.buffer_attribs.reserve(header.attribCount);
    for (uint32_t i = 0; i < header.attribCount; i++) {
        uint32_t name = 0;
        uint32_t typeLen = 0;
        BufferAttribute attrib;
        if (!readU32(name) || !readU32(attrib.componentType) || !readU32(attrib.offset) || !readU32(typeLen)) {
            RP_CORE_ERROR("Mesh blob attribute table is truncated");
            return nullptr;
        }
        if (offset + typeLen > blob.size()) {
            RP_CORE_ERROR("Mesh blob attribute type is truncated");
            return nullptr;
        }
        attrib.name = static_cast<BufferAttributeID>(name);
        attrib.type = std::string(reinterpret_cast<const char *>(blob.data() + offset), typeLen);
        offset += typeLen;
        params.bufferLayout.buffer_attribs.push_back(std::move(attrib));
    }

    std::vector<uint8_t> vertexStorage(blob.begin() + header.vertexDataOffset,
                                       blob.begin() + header.vertexDataOffset + header.vertexDataSize);
    std::vector<uint8_t> indexStorage(blob.begin() + header.indexDataOffset,
                                      blob.begin() + header.indexDataOffset + header.indexDataSize);

    params.vertexData = vertexStorage.data();
    params.indexData = indexStorage.data();

    return std::make_unique<Mesh>(params);
}

} // namespace Rapture

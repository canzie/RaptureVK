#include "Mesh.h"

#include "app/Application.h"
#include "gpu/acceleration_structures/AccelerationStructureBuilder.h"
#include "gpu/acceleration_structures/BLAS.h"
#include "gpu/buffers/BufferPool.h"

#include "core/utils/GLTypes.h"
#include "core/utils/Log.h"

#include <cstring>

namespace Rapture {

// Fixed 128-byte directory at the start of every mesh blob. The reserved tail absorbs new fields
// without moving the data, and the section offsets let readers jump straight to the bytes. When the
// reserved space runs out, grow the header and bump the major version.
struct MeshBlobHeader {
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
    float boundsMin[3] = {};
    float boundsMax[3] = {};
    uint32_t sectionCount = 0;
    uint32_t sectionOffset = 0; // byte offset of the section table
    uint32_t reserved[12] = {}; // pad to 128 bytes, consume for backward-compatible additions
};

static_assert(sizeof(MeshBlobHeader) == 128, "mesh blob header is a fixed 128-byte directory, bump to 256 if it must grow");

// std::unique_ptr<DescriptorSubAllocationBase<Buffer>> Mesh::s_bindlessMeshDataAllocation = nullptr;

Mesh::Mesh(MeshAllocatorParams &params)
{
    setMeshData(params);
}

Mesh::Mesh() : m_indexCount(0), m_vertexBuffer(nullptr), m_indexBuffer(nullptr) {}

Mesh::~Mesh()
{
    m_blas.reset();
    m_indexAllocation.reset();
    m_vertexAllocation.reset();
}

Mesh::Mesh(Mesh &&other) noexcept = default;

Mesh &Mesh::operator=(Mesh &&other) noexcept = default;

VkIndexType Mesh::indexTypeFromComponentType(uint32_t componentType)
{
    switch (componentType) {
    case UNSIGNED_INT_TYPE:
        return VK_INDEX_TYPE_UINT32;
    case UNSIGNED_SHORT_TYPE:
        return VK_INDEX_TYPE_UINT16;
    }

    RP_CORE_ERROR("Indices cannot use component type {}, reading them as 16 bit", componentType);
    return VK_INDEX_TYPE_UINT16;
}

void Mesh::setBounds(const glm::vec3 &min, const glm::vec3 &max)
{
    m_boundsMin = min;
    m_boundsMax = max;
}

bool Mesh::requestBLAS(AssetHandle assetHandle)
{
    if (m_blas != nullptr) {
        return true;
    }

    auto blas = std::make_unique<BLAS>(*this);
    if (!blas->isValid()) {
        RP_CORE_ERROR("Failed to create the acceleration structure");
        return false;
    }

    m_blas = std::move(blas);

    auto &rc = Application::getInstance().getVulkanContext().getRenderContext();
    rc.accelerationStructureBuilder->enqueue(*this, assetHandle);

    return true;
}

uint64_t Mesh::getSizeBytes() const
{
    uint64_t size = 0;
    if (m_vertexAllocation) {
        size += m_vertexAllocation->sizeBytes;
    }
    if (m_indexAllocation) {
        size += m_indexAllocation->sizeBytes;
    }
    return size;
}

void Mesh::setMeshData(MeshAllocatorParams &params)
{
    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();

    m_indexCount = params.indexCount;
    m_boundsMin = params.boundsMin;
    m_boundsMax = params.boundsMax;

    m_sections = params.sections;
    if (m_sections.empty()) {
        m_sections.push_back({0, params.indexCount});
    }

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

std::vector<uint8_t> Mesh::serializeGeometry() const
{
    if (!m_vertexAllocation || !m_indexAllocation || !m_vertexBuffer || !m_indexBuffer) {
        RP_CORE_ERROR("mesh holds no geometry to read back");
        return {};
    }

    std::vector<uint8_t> vertexBytes(m_vertexAllocation->sizeBytes);
    std::vector<uint8_t> indexBytes(m_indexAllocation->sizeBytes);
    m_vertexAllocation->downloadData(vertexBytes.data(), vertexBytes.size());
    m_indexAllocation->downloadData(indexBytes.data(), indexBytes.size());

    MeshAllocatorParams params;
    params.vertexData = vertexBytes.data();
    params.vertexDataSize = static_cast<uint32_t>(vertexBytes.size());
    params.indexData = indexBytes.data();
    params.indexDataSize = static_cast<uint32_t>(indexBytes.size());
    params.indexCount = m_indexCount;
    params.indexType = m_indexBuffer->getIndexType();
    params.boundsMin = m_boundsMin;
    params.boundsMax = m_boundsMax;
    params.bufferLayout = m_vertexBuffer->getBufferLayout();
    params.sections = m_sections;

    return params.serialize();
}

std::vector<uint8_t> MeshAllocatorParams::serialize() const
{
    uint32_t attribBytes = 0;
    for (const auto &attrib : bufferLayout.buffer_attribs) {
        attribBytes += sizeof(uint32_t) * 4 + static_cast<uint32_t>(BufferAttributeType_toString(attrib.type).size());
    }

    MeshBlobHeader header;
    header.vertexDataSize = vertexDataSize;
    header.indexDataSize = indexDataSize;
    header.indexCount = indexCount;
    header.indexType = static_cast<uint32_t>(indexType);
    header.attribCount = static_cast<uint32_t>(bufferLayout.buffer_attribs.size());
    header.isInterleaved = bufferLayout.isInterleaved ? 1u : 0u;
    header.vertexSize = bufferLayout.vertexSize;
    header.binding = bufferLayout.binding;
    header.flags = bufferLayout.flags;
    header.attribOffset = sizeof(MeshBlobHeader);
    header.vertexDataOffset = header.attribOffset + attribBytes;
    header.indexDataOffset = header.vertexDataOffset + vertexDataSize;
    header.sectionCount = static_cast<uint32_t>(sections.size());
    header.sectionOffset = header.indexDataOffset + indexDataSize;
    std::memcpy(header.boundsMin, &boundsMin, sizeof(header.boundsMin));
    std::memcpy(header.boundsMax, &boundsMax, sizeof(header.boundsMax));

    std::vector<uint8_t> blob(header.sectionOffset + sections.size() * sizeof(MeshSection));
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
        // the type stays a string on the wire, so a blob written before it became an enum still reads
        std::string_view typeName = BufferAttributeType_toString(attrib.type);
        writeU32(static_cast<uint32_t>(typeName.size()));
        std::memcpy(blob.data() + offset, typeName.data(), typeName.size());
        offset += typeName.size();
    }

    if (vertexData != nullptr && vertexDataSize > 0) {
        std::memcpy(blob.data() + header.vertexDataOffset, vertexData, vertexDataSize);
    }
    if (indexData != nullptr && indexDataSize > 0) {
        std::memcpy(blob.data() + header.indexDataOffset, indexData, indexDataSize);
    }
    if (!sections.empty()) {
        std::memcpy(blob.data() + header.sectionOffset, sections.data(), sections.size() * sizeof(MeshSection));
    }

    return blob;
}

bool MeshAllocatorParams::deserialize(std::span<const uint8_t> blob, MeshAllocatorParams &params)
{
    if (blob.size() < sizeof(MeshBlobHeader)) {
        RP_CORE_ERROR("Mesh blob is smaller than its header");
        return false;
    }

    MeshBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(MeshBlobHeader));

    if (header.vertexDataOffset + header.vertexDataSize > blob.size() ||
        header.indexDataOffset + header.indexDataSize > blob.size()) {
        RP_CORE_ERROR("Mesh blob data sections are out of range");
        return false;
    }

    params.vertexDataSize = header.vertexDataSize;
    params.indexDataSize = header.indexDataSize;
    params.indexCount = header.indexCount;
    // blobs written before the field became an index type hold a glTF component type, whose values
    // cannot collide with one
    params.indexType = header.indexType >= UNSIGNED_BYTE_TYPE ? Mesh::indexTypeFromComponentType(header.indexType)
                                                              : static_cast<VkIndexType>(header.indexType);
    params.bufferLayout.isInterleaved = header.isInterleaved != 0;
    params.bufferLayout.vertexSize = header.vertexSize;
    params.bufferLayout.binding = header.binding;
    params.bufferLayout.flags = header.flags;
    params.boundsMin = glm::vec3(header.boundsMin[0], header.boundsMin[1], header.boundsMin[2]);
    params.boundsMax = glm::vec3(header.boundsMax[0], header.boundsMax[1], header.boundsMax[2]);

    size_t sectionBytes = header.sectionCount * sizeof(MeshSection);
    if (header.sectionOffset + sectionBytes > blob.size()) {
        RP_CORE_ERROR("Mesh blob section table is out of range");
        return false;
    }
    params.sections.resize(header.sectionCount);
    if (header.sectionCount > 0) {
        std::memcpy(params.sections.data(), blob.data() + header.sectionOffset, sectionBytes);
    }

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
            return false;
        }
        if (offset + typeLen > blob.size()) {
            RP_CORE_ERROR("Mesh blob attribute type is truncated");
            return false;
        }
        attrib.name = static_cast<BufferAttributeID>(name);
        attrib.type =
            BufferAttributeType_fromString(std::string_view(reinterpret_cast<const char *>(blob.data() + offset), typeLen));
        offset += typeLen;
        params.bufferLayout.buffer_attribs.push_back(std::move(attrib));
    }

    params.vertexData = const_cast<uint8_t *>(blob.data() + header.vertexDataOffset);
    params.indexData = const_cast<uint8_t *>(blob.data() + header.indexDataOffset);

    return true;
}

} // namespace Rapture

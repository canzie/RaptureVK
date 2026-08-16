#include "MaterialGraph.h"

#include "assets/asset_manager/AssetManager.h"
#include "core/utils/Log.h"
#include "gpu/textures/Texture.h"

#include <cstring>

namespace Rapture {

static constexpr uint32_t GRAPH_BLOB_MAGIC = 0x52475052; // "RPGR", identifies the blob as a material graph

// Version packs major in the high 16 bits and minor in the low 16, matching the other asset blobs.
static constexpr uint16_t GRAPH_BLOB_VERSION_MAJOR = 1;
static constexpr uint16_t GRAPH_BLOB_VERSION_MINOR = 0;
static constexpr uint32_t GRAPH_BLOB_VERSION = (static_cast<uint32_t>(GRAPH_BLOB_VERSION_MAJOR) << 16) | GRAPH_BLOB_VERSION_MINOR;

struct GraphBlobHeader {
    uint32_t magic = GRAPH_BLOB_MAGIC;
    uint32_t version = GRAPH_BLOB_VERSION;
    uint32_t outputNodeId = 0;
    uint32_t nodeCount = 0;
    uint32_t connectionCount = 0;
    uint32_t nodesOffset = 0;       // byte offset of the node section
    uint32_t connectionsOffset = 0; // byte offset of the connection section
    uint32_t reserved = 0;
};

const GraphNode *MaterialGraph::findNode(uint32_t id) const
{
    for (const auto &node : nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

std::vector<uint8_t> MaterialGraph::serialize() const
{
    auto appendBytes = [](std::vector<uint8_t> &out, const void *data, size_t size) {
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        out.insert(out.end(), bytes, bytes + size);
    };
    auto appendU32 = [&](std::vector<uint8_t> &out, uint32_t v) { appendBytes(out, &v, sizeof(v)); };
    auto appendU64 = [&](std::vector<uint8_t> &out, uint64_t v) { appendBytes(out, &v, sizeof(v)); };
    auto appendString = [&](std::vector<uint8_t> &out, std::string_view s) {
        appendU32(out, static_cast<uint32_t>(s.size()));
        appendBytes(out, s.data(), s.size());
    };

    std::vector<uint8_t> nameSection;
    appendString(nameSection, name);
    appendString(nameSection, Graph_domainName(domain));

    std::vector<uint8_t> nodeSection;
    for (const GraphNode &node : nodes) {
        appendU32(nodeSection, node.id);
        appendString(nodeSection, Graph_nodeTypeName(node.type));

        appendU32(nodeSection, static_cast<uint32_t>(node.inputValues.size()));
        for (const std::optional<PinValue> &value : node.inputValues) {
            uint8_t present = value.has_value() ? 1u : 0u;
            appendBytes(nodeSection, &present, sizeof(present));
            if (value.has_value()) {
                appendBytes(nodeSection, &value.value(), sizeof(PinValue));
            }
        }

        appendU32(nodeSection, static_cast<uint32_t>(node.inputTextures.size()));
        for (const AssetPtr<Texture> &texture : node.inputTextures) {
            AssetHandle handle = texture ? texture.ref().get()->getHandle() : INVALID_ASSET_HANDLE;
            appendU64(nodeSection, handle);
        }
    }

    std::vector<uint8_t> connectionSection;
    for (const GraphConnection &connection : connections) {
        appendU32(connectionSection, connection.srcNode);
        appendU32(connectionSection, connection.srcPin);
        appendU32(connectionSection, connection.dstNode);
        appendU32(connectionSection, connection.dstPin);
    }

    GraphBlobHeader header;
    header.outputNodeId = outputNodeId;
    header.nodeCount = static_cast<uint32_t>(nodes.size());
    header.connectionCount = static_cast<uint32_t>(connections.size());
    header.nodesOffset = static_cast<uint32_t>(sizeof(GraphBlobHeader) + nameSection.size());
    header.connectionsOffset = static_cast<uint32_t>(header.nodesOffset + nodeSection.size());

    std::vector<uint8_t> blob(sizeof(GraphBlobHeader));
    std::memcpy(blob.data(), &header, sizeof(GraphBlobHeader));
    blob.insert(blob.end(), nameSection.begin(), nameSection.end());
    blob.insert(blob.end(), nodeSection.begin(), nodeSection.end());
    blob.insert(blob.end(), connectionSection.begin(), connectionSection.end());
    return blob;
}

std::optional<MaterialGraph> MaterialGraph::deserialize(std::span<const uint8_t> blob)
{
    if (blob.size() < sizeof(GraphBlobHeader)) {
        RP_CORE_ERROR("Graph blob is smaller than its header");
        return std::nullopt;
    }

    GraphBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(GraphBlobHeader));
    if (header.magic != GRAPH_BLOB_MAGIC) {
        RP_CORE_ERROR("Graph blob has an invalid magic");
        return std::nullopt;
    }
    if ((header.version >> 16) != GRAPH_BLOB_VERSION_MAJOR) {
        RP_CORE_ERROR("Graph blob major version {} is unsupported", header.version >> 16);
        return std::nullopt;
    }

    size_t offset = 0;
    auto read = [&](void *dst, size_t size) -> bool {
        if (offset + size > blob.size()) {
            return false;
        }
        std::memcpy(dst, blob.data() + offset, size);
        offset += size;
        return true;
    };
    auto readU32 = [&](uint32_t &v) { return read(&v, sizeof(v)); };
    auto readU64 = [&](uint64_t &v) { return read(&v, sizeof(v)); };
    auto readString = [&](std::string &s) -> bool {
        uint32_t length = 0;
        if (!readU32(length) || offset + length > blob.size()) {
            return false;
        }
        s.assign(reinterpret_cast<const char *>(blob.data() + offset), length);
        offset += length;
        return true;
    };

    MaterialGraph graph;
    graph.outputNodeId = header.outputNodeId;

    offset = sizeof(GraphBlobHeader);
    std::string domainName;
    if (!readString(graph.name) || !readString(domainName)) {
        RP_CORE_ERROR("Graph blob name section is truncated");
        return std::nullopt;
    }
    graph.domain = Graph_domainFromName(domainName);

    offset = header.nodesOffset;
    graph.nodes.reserve(header.nodeCount);
    for (uint32_t i = 0; i < header.nodeCount; ++i) {
        GraphNode node;
        std::string typeName;
        if (!readU32(node.id) || !readString(typeName)) {
            RP_CORE_ERROR("Graph blob node {} is truncated", i);
            return std::nullopt;
        }
        node.type = Graph_nodeTypeFromName(typeName);

        uint32_t valueCount = 0;
        if (!readU32(valueCount)) {
            return std::nullopt;
        }
        node.inputValues.reserve(valueCount);
        for (uint32_t v = 0; v < valueCount; ++v) {
            uint8_t present = 0;
            if (!read(&present, sizeof(present))) {
                return std::nullopt;
            }
            if (present != 0) {
                PinValue value;
                if (!read(&value, sizeof(PinValue))) {
                    return std::nullopt;
                }
                node.inputValues.push_back(value);
            } else {
                node.inputValues.push_back(std::nullopt);
            }
        }

        uint32_t textureCount = 0;
        if (!readU32(textureCount)) {
            return std::nullopt;
        }
        node.inputTextures.reserve(textureCount);
        for (uint32_t t = 0; t < textureCount; ++t) {
            uint64_t handle = INVALID_ASSET_HANDLE;
            if (!readU64(handle)) {
                return std::nullopt;
            }
            node.inputTextures.push_back(handle != INVALID_ASSET_HANDLE ? AssetPtr<Texture>(AssetManager::getAsset(handle))
                                                                        : AssetPtr<Texture>{});
        }

        graph.nodes.push_back(std::move(node));
    }

    offset = header.connectionsOffset;
    graph.connections.reserve(header.connectionCount);
    for (uint32_t i = 0; i < header.connectionCount; ++i) {
        GraphConnection connection;
        if (!readU32(connection.srcNode) || !readU32(connection.srcPin) || !readU32(connection.dstNode) ||
            !readU32(connection.dstPin)) {
            RP_CORE_ERROR("Graph blob connection {} is truncated", i);
            return std::nullopt;
        }
        graph.connections.push_back(connection);
    }

    return graph;
}

} // namespace Rapture

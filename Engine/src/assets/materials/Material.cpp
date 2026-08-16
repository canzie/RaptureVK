#include "Material.h"

#include "assets/asset_manager/AssetManager.h"
#include "assets/asset_manager/ReservedAssets.h"
#include "gpu/buffers/FreeListStorageBuffer.h"
#include "gpu/buffers/VirtualStorageBuffer.h"
#include "core/events/ProjectEvents.h"
#include "graph/MaterialGraphCompiler.h"
#include "graph/SurfaceGraphManager.h"
#include "core/utils/Log.h"
#include "gpu/textures/Texture.h"
#include "core/utils/rp_assert.h"

#include <glm/glm.hpp>

#include <cstring>

namespace Rapture {

static constexpr uint32_t MATERIAL_BLOB_MAGIC = 0x424D5052; // "RPMB", identifies the blob as a base material

// Version packs major in the high 16 bits and minor in the low 16, matching the other asset blobs.
static constexpr uint16_t MATERIAL_BLOB_VERSION_MAJOR = 1;
static constexpr uint16_t MATERIAL_BLOB_VERSION_MINOR = 0;
static constexpr uint32_t MATERIAL_BLOB_VERSION =
    (static_cast<uint32_t>(MATERIAL_BLOB_VERSION_MAJOR) << 16) | MATERIAL_BLOB_VERSION_MINOR;

struct MaterialBlobHeader {
    uint32_t magic = MATERIAL_BLOB_MAGIC;
    uint32_t version = MATERIAL_BLOB_VERSION;
    uint32_t tableCount = 0;
    uint32_t nameOffset = 0;
    uint32_t tableOffset = 0;
    uint32_t graphOffset = 0;
    uint32_t reserved[2] = {};
};

// The graph data arena is a byte pool sub-allocated in fixed blocks; a graph instance takes as
// many blocks as its packed slice needs
static constexpr uint32_t GRAPH_ARENA_BLOCKS = 512;
static constexpr uint32_t GRAPH_BLOCK_BYTES = 32;

bool MaterialManager::s_initialized = false;
uint32_t MaterialManager::s_defaultTextureIndex = 0;
std::unordered_map<std::string, AssetHandle> MaterialManager::s_materialHandles;
std::unique_ptr<FreeListStorageBuffer> MaterialManager::s_materialBuffer;
std::unique_ptr<VirtualStorageBuffer> MaterialManager::s_graphBuffer;
std::unique_ptr<SurfaceGraphManager> MaterialManager::s_surfaceGraphManager;
EventListenerId MaterialManager::s_serializeListener = 0;
EventListenerId MaterialManager::s_registerListener = 0;
EventListenerId MaterialManager::s_registerCompleteListener = 0;

BaseMaterial::BaseMaterial(std::string name, uint32_t graphId, std::unordered_map<ParameterId, uint32_t> table, MaterialGraph graph)
    : m_name(std::move(name)), m_graphId(graphId), m_table(std::move(table)), m_graph(std::move(graph))
{
}

bool BaseMaterial::tryGetOffset(const ParameterId &id, uint32_t &out) const
{
    auto it = m_table.find(id);
    if (it == m_table.end()) return false;
    out = it->second;
    return true;
}

std::vector<uint8_t> BaseMaterial::serialize() const
{
    auto appendBytes = [](std::vector<uint8_t> &out, const void *data, size_t size) {
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        out.insert(out.end(), bytes, bytes + size);
    };
    auto appendU32 = [&](std::vector<uint8_t> &out, uint32_t v) { appendBytes(out, &v, sizeof(v)); };
    auto appendString = [&](std::vector<uint8_t> &out, std::string_view s) {
        appendU32(out, static_cast<uint32_t>(s.size()));
        appendBytes(out, s.data(), s.size());
    };

    std::vector<uint8_t> nameSection;
    appendString(nameSection, m_name);

    std::vector<uint8_t> tableSection;
    for (const auto &[param, slotOffset] : m_table) {
        appendString(tableSection, param);
        appendU32(tableSection, slotOffset);
    }

    std::vector<uint8_t> graphSection = m_graph.serialize();

    MaterialBlobHeader header;
    header.tableCount = static_cast<uint32_t>(m_table.size());
    header.nameOffset = sizeof(MaterialBlobHeader);
    header.tableOffset = static_cast<uint32_t>(header.nameOffset + nameSection.size());
    header.graphOffset = static_cast<uint32_t>(header.tableOffset + tableSection.size());

    std::vector<uint8_t> blob(sizeof(MaterialBlobHeader));
    std::memcpy(blob.data(), &header, sizeof(MaterialBlobHeader));
    blob.insert(blob.end(), nameSection.begin(), nameSection.end());
    blob.insert(blob.end(), tableSection.begin(), tableSection.end());
    blob.insert(blob.end(), graphSection.begin(), graphSection.end());
    return blob;
}

std::unique_ptr<BaseMaterial> BaseMaterial::deserialize(std::span<const uint8_t> blob)
{
    if (blob.size() < sizeof(MaterialBlobHeader)) {
        RP_CORE_ERROR("Material blob is smaller than its header");
        return nullptr;
    }

    MaterialBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(MaterialBlobHeader));
    if (header.magic != MATERIAL_BLOB_MAGIC) {
        RP_CORE_ERROR("Material blob has an invalid magic");
        return nullptr;
    }
    if ((header.version >> 16) != MATERIAL_BLOB_VERSION_MAJOR) {
        RP_CORE_ERROR("Material blob major version {} is unsupported", header.version >> 16);
        return nullptr;
    }
    if (header.graphOffset > blob.size()) {
        RP_CORE_ERROR("Material blob graph offset is out of range");
        return nullptr;
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
    auto readString = [&](std::string &s) -> bool {
        uint32_t length = 0;
        if (!readU32(length) || offset + length > blob.size()) {
            return false;
        }
        s.assign(reinterpret_cast<const char *>(blob.data() + offset), length);
        offset += length;
        return true;
    };

    offset = header.nameOffset;
    std::string name;
    if (!readString(name)) {
        RP_CORE_ERROR("Material blob name is truncated");
        return nullptr;
    }

    offset = header.tableOffset;
    std::unordered_map<ParameterId, uint32_t> table;
    table.reserve(header.tableCount);
    for (uint32_t i = 0; i < header.tableCount; ++i) {
        std::string param;
        uint32_t slotOffset = 0;
        if (!readString(param) || !readU32(slotOffset)) {
            RP_CORE_ERROR("Material blob parameter table is truncated");
            return nullptr;
        }
        table.emplace(std::move(param), slotOffset);
    }

    std::optional<MaterialGraph> graph = MaterialGraph::deserialize(blob.subspan(header.graphOffset));
    if (!graph) {
        return nullptr;
    }

    // Recompile the graph to obtain a runtime id; the serialized table offsets match its layout
    uint32_t graphId = MaterialManager::getSurfaceGraphManager().registerGraph(*graph);
    if (graphId == UINT32_MAX) {
        RP_CORE_ERROR("Failed to recompile material graph for '{}'", name);
        return nullptr;
    }

    return std::make_unique<BaseMaterial>(std::move(name), graphId, std::move(table), std::move(*graph));
}

void MaterialManager::init()
{
    if (s_initialized) {
        RP_CORE_WARN("MaterialManager already initialized");
        return;
    }

    s_materialHandles.clear();

    s_materialBuffer = std::make_unique<FreeListStorageBuffer>(sizeof(MaterialData), MAX_MATERIALS,
                                                               DescriptorSetBindingLocation::MATERIAL_DATA_SSBO);
    s_graphBuffer = std::make_unique<VirtualStorageBuffer>(static_cast<VkDeviceSize>(GRAPH_ARENA_BLOCKS) * GRAPH_BLOCK_BYTES,
                                                           GRAPH_BLOCK_BYTES, DescriptorSetBindingLocation::GRAPH_DATA_SSBO);

    s_surfaceGraphManager = std::make_unique<SurfaceGraphManager>();

    auto asset = AssetManager::importDefaultAsset(ASSET_TEXTURE);
    auto defaultTexture = asset ? asset.get()->getUnderlyingAsset<Texture>() : nullptr;
    if (defaultTexture && defaultTexture->isReady()) {
        s_defaultTextureIndex = defaultTexture->getBindlessIndex();
    } else {
        RP_CORE_ERROR("Failed to get default white texture index");
        s_defaultTextureIndex = 0;
    }

    s_initialized = true;
    createDefaultMaterials();

    s_serializeListener = ProjectEvents::onProjectSerialize().addListener([](WriteNode &root) { (void)root; });
    s_registerListener = ProjectEvents::onProjectRegister().addListener([](ReadNode &root) { (void)root; });
    s_registerCompleteListener = ProjectEvents::onProjectRegisterComplete().addListener([]() {});
}

void MaterialManager::releaseGraphResources()
{
    s_materialHandles.clear();
    s_surfaceGraphManager.reset();
}

void MaterialManager::shutdown()
{
    ProjectEvents::onProjectSerialize().removeListener(s_serializeListener);
    ProjectEvents::onProjectRegister().removeListener(s_registerListener);
    ProjectEvents::onProjectRegisterComplete().removeListener(s_registerCompleteListener);
    s_materialHandles.clear();
    s_surfaceGraphManager.reset();
    s_materialBuffer.reset();
    s_graphBuffer.reset();
    s_initialized = false;
}

SurfaceGraphManager &MaterialManager::getSurfaceGraphManager()
{
    RP_ASSERT(s_surfaceGraphManager != nullptr, "Initialise the material manager first");
    return *s_surfaceGraphManager;
}

uint32_t MaterialManager::allocateSlot()
{
    RP_ASSERT(s_materialBuffer != nullptr, "Initialise the material manager first");
    return s_materialBuffer->allocate();
}

void MaterialManager::freeSlot(uint32_t slot)
{
    RP_ASSERT(s_materialBuffer != nullptr, "Initialise the material manager first");
    s_materialBuffer->free(slot);
}

void MaterialManager::writeSlot(uint32_t slot, const MaterialData &data)
{
    RP_ASSERT(s_materialBuffer != nullptr, "Initialise the material manager first");
    s_materialBuffer->write(slot, &data);
}

VirtualStorageBuffer::Allocation MaterialManager::allocateGraphData(uint32_t sizeBytes)
{
    RP_ASSERT(s_graphBuffer != nullptr, "Initialise the material manager first");
    return s_graphBuffer->allocate(sizeBytes);
}

void MaterialManager::freeGraphData(VirtualStorageBuffer::Allocation &allocation)
{
    RP_ASSERT(s_graphBuffer != nullptr, "Initialise the material manager first");
    s_graphBuffer->free(allocation);
}

void MaterialManager::writeGraphData(const VirtualStorageBuffer::Allocation &allocation, std::span<const uint32_t> data,
                                     uint32_t uintOffset)
{
    RP_ASSERT(s_graphBuffer != nullptr, "Initialise the material manager first");
    s_graphBuffer->write(allocation, std::as_bytes(data), static_cast<VkDeviceSize>(uintOffset) * sizeof(uint32_t));
}

static void s_createGltfBaseMaterial()
{
    AssetPtr<Texture> white(AssetManager::importDefaultAsset(ASSET_TEXTURE));
    AssetPtr<Texture> flatNormal(AssetManager::registerReservedAsset(
        RE_FLAT_NORMAL_TEXTURE, Texture::createDefaultFlatNormalTexture(), "<default_flat_normal>", ASSET_TEXTURE));

    using GN = GraphNodeType;
    MaterialGraph graph;
    graph.name = "glTF";

    graph.nodes.push_back({.id = 1, .type = GN::TEXCOORD});

    graph.nodes.push_back({.id = 2, .type = GN::TEXTURE_SAMPLE, .inputTextures = {white}});
    graph.nodes.push_back({.id = 3, .type = GN::CONSTANT_VEC4, .inputValues = {PinValue(glm::vec4(1.0f))}});
    graph.nodes.push_back({.id = 4, .type = GN::MULTIPLY_VEC3});

    graph.nodes.push_back({.id = 5, .type = GN::TEXTURE_SAMPLE, .inputTextures = {flatNormal}});
    graph.nodes.push_back({.id = 6, .type = GN::NORMAL_MAP_RG});

    graph.nodes.push_back({.id = 7, .type = GN::TEXTURE_SAMPLE, .inputTextures = {white}});
    graph.nodes.push_back({.id = 8, .type = GN::SPLIT_VEC4});
    graph.nodes.push_back({.id = 9, .type = GN::CONSTANT_FLOAT, .inputValues = {PinValue(1.0f)}});
    graph.nodes.push_back({.id = 10, .type = GN::MULTIPLY_FLOAT});
    graph.nodes.push_back({.id = 11, .type = GN::CONSTANT_FLOAT, .inputValues = {PinValue(1.0f)}});
    graph.nodes.push_back({.id = 12, .type = GN::MULTIPLY_FLOAT});

    graph.nodes.push_back({.id = 13, .type = GN::TEXTURE_SAMPLE, .inputTextures = {white}});
    graph.nodes.push_back({.id = 14, .type = GN::CONSTANT_FLOAT, .inputValues = {PinValue(1.0f)}});
    graph.nodes.push_back({.id = 15, .type = GN::MULTIPLY_FLOAT});

    graph.nodes.push_back({.id = 16, .type = GN::TEXTURE_SAMPLE, .inputTextures = {white}});
    graph.nodes.push_back({.id = 17, .type = GN::CONSTANT_VEC3, .inputValues = {PinValue(glm::vec3(1.0f))}});
    graph.nodes.push_back({.id = 18, .type = GN::MULTIPLY_VEC3});
    graph.nodes.push_back({.id = 19, .type = GN::CONSTANT_FLOAT, .inputValues = {PinValue(0.0f)}});

    graph.nodes.push_back({.id = 20, .type = GN::SURFACE_OUTPUT});
    graph.outputNodeId = 20;

    graph.connections = {
        {1, 0, 2, 1},   {1, 0, 5, 1},   {1, 0, 7, 1},   {1, 0, 13, 1},  {1, 0, 16, 1},  {2, 0, 4, 0},
        {3, 0, 4, 1},   {4, 0, 20, 0},  {5, 0, 6, 0},   {6, 0, 20, 1},  {7, 0, 8, 0},   {8, 1, 10, 0},
        {9, 0, 10, 1},  {10, 0, 20, 2}, {8, 2, 12, 0},  {11, 0, 12, 1}, {12, 0, 20, 3}, {13, 0, 15, 0},
        {14, 0, 15, 1}, {15, 0, 20, 4}, {16, 0, 18, 0}, {17, 0, 18, 1}, {18, 0, 20, 5}, {19, 0, 20, 6},
    };

    SurfaceGraphManager &graphs = MaterialManager::getSurfaceGraphManager();
    uint32_t graphId = graphs.registerGraph(graph);
    if (graphId == UINT32_MAX) {
        RP_CORE_ERROR("Failed to compile the glTF base material graph");
        return;
    }

    GraphSlotMapping mapping = graphs.getMapping(graphId);
    std::unordered_map<ParameterId, uint32_t> table;
    auto bind = [&](std::string_view id, uint32_t nodeId) {
        auto slot = mapping.slots.find(Graph_pinKey(nodeId, 0));
        if (slot != mapping.slots.end()) {
            table[ParameterId(id)] = slot->second.offset;
        }
    };
    bind(MP_ALBEDO_MAP, 2);
    bind(MP_ALBEDO, 3);
    bind(MP_NORMAL_MAP, 5);
    bind(MP_METALLIC_ROUGHNESS_MAP, 7);
    bind(MP_ROUGHNESS, 9);
    bind(MP_METALLIC, 11);
    bind(MP_AO_MAP, 13);
    bind(MP_AO, 14);
    bind(MP_EMISSIVE_MAP, 16);
    bind(MP_EMISSIVE, 17);
    bind(MP_EMISSIVE_STRENGTH, 19);

    MaterialManager::createBuiltinMaterial("glTF Base Material", graphId, std::move(table), std::move(graph),
                                           RE_GLTF_BASE_MATERIAL);
}

void MaterialManager::createDefaultMaterials()
{
    // Blender-style default surface: a bare output node resolves to white albedo, roughness 0.5,
    // metallic 0 and ao 1 from the SurfaceData fallbacks
    MaterialGraph graph;
    graph.name = "Default";
    graph.domain = GD_SURFACE;
    graph.nodes.push_back({.id = 1, .type = GraphNodeType::SURFACE_OUTPUT});
    graph.outputNodeId = 1;

    uint32_t graphId = s_surfaceGraphManager->registerGraph(graph);
    if (graphId == UINT32_MAX) {
        RP_CORE_ERROR("Failed to compile the default material graph");
        return;
    }

    createBuiltinMaterial("Default Material", graphId, {}, std::move(graph), RE_DEFAULT_MATERIAL);

    // The same bare output in the terrain domain, so terrain starts white and is authored from there
    MaterialGraph terrainGraph;
    terrainGraph.name = "Terrain";
    terrainGraph.domain = GD_TERRAIN;
    terrainGraph.nodes.push_back({.id = 1, .type = GraphNodeType::SURFACE_OUTPUT});
    terrainGraph.outputNodeId = 1;

    uint32_t terrainGraphId = s_surfaceGraphManager->registerGraph(terrainGraph);
    if (terrainGraphId == UINT32_MAX) {
        RP_CORE_ERROR("Failed to compile the terrain material graph");
        return;
    }

    createBuiltinMaterial("Terrain Base Material", terrainGraphId, {}, std::move(terrainGraph), RE_TERRAIN_MATERIAL);

    s_createGltfBaseMaterial();
}

AssetPtr<BaseMaterial> MaterialManager::getMaterial(const std::string &name)
{
    auto it = s_materialHandles.find(name);
    if (it == s_materialHandles.end()) {
        return {};
    }
    return AssetPtr<BaseMaterial>(AssetManager::getAsset(it->second));
}

AssetPtr<BaseMaterial> MaterialManager::createMaterial(const std::string &name, uint32_t graphId,
                                                       std::unordered_map<ParameterId, uint32_t> table, MaterialGraph graph,
                                                       std::filesystem::path outputFolder)
{
    RP_ASSERT(s_initialized, "Initialise the material manager first");

    if (s_materialHandles.find(name) != s_materialHandles.end()) {
        RP_CORE_ERROR("Material '{}' already exists", name);
        return {};
    }

    auto material = std::make_unique<BaseMaterial>(name, graphId, std::move(table), std::move(graph));
    AssetRef ref = AssetManager::importAsset(AssetImportDataRequest{
        .data = BaseMaterialImportData{std::move(material)}, .output = std::move(outputFolder), .name = name});
    if (!ref) {
        return {};
    }

    s_materialHandles[name] = ref.get()->getHandle();
    return AssetPtr<BaseMaterial>(std::move(ref));
}

AssetPtr<BaseMaterial> MaterialManager::createBuiltinMaterial(const std::string &name, uint32_t graphId,
                                                              std::unordered_map<ParameterId, uint32_t> table, MaterialGraph graph,
                                                              AssetHandle reservedHandle)
{
    RP_ASSERT(s_initialized, "Initialise the material manager first");

    if (s_materialHandles.find(name) != s_materialHandles.end()) {
        RP_CORE_ERROR("Material '{}' already exists", name);
        return {};
    }

    auto material = std::make_unique<BaseMaterial>(name, graphId, std::move(table), std::move(graph));
    AssetRef ref = AssetManager::registerReservedAsset(reservedHandle, std::move(material), name, ASSET_MATERIAL);
    if (!ref) {
        return {};
    }

    s_materialHandles[name] = ref.get()->getHandle();
    return AssetPtr<BaseMaterial>(std::move(ref));
}

uint32_t MaterialManager::getDefaultTextureIndex()
{
    return s_defaultTextureIndex;
}

void MaterialManager::printMaterialNames()
{
    for (const auto &[name, handle] : s_materialHandles) {
        RP_CORE_INFO("\t {}", name);
    }
}

} // namespace Rapture

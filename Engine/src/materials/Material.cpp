#include "Material.h"

#include "asset_manager/AssetManager.h"
#include "buffers/FreeListStorageBuffer.h"
#include "buffers/VirtualStorageBuffer.h"
#include "graph/SurfaceGraphManager.h"
#include "logging/Log.h"
#include "textures/Texture.h"
#include "utils/rp_assert.h"

namespace Rapture {

// The graph data arena is a byte pool sub-allocated in fixed blocks; a graph instance takes as
// many blocks as its packed slice needs
static constexpr uint32_t GRAPH_ARENA_BLOCKS = 512;
static constexpr uint32_t GRAPH_BLOCK_BYTES = 32;

bool MaterialManager::s_initialized = false;
uint32_t MaterialManager::s_defaultTextureIndex = 0;
std::unordered_map<std::string, std::shared_ptr<BaseMaterial>> MaterialManager::s_materials;
std::unique_ptr<FreeListStorageBuffer> MaterialManager::s_materialBuffer;
std::unique_ptr<VirtualStorageBuffer> MaterialManager::s_graphBuffer;
std::unique_ptr<SurfaceGraphManager> MaterialManager::s_surfaceGraphManager;

BaseMaterial::BaseMaterial(std::string name, uint32_t graphId, std::unordered_map<ParameterID, uint32_t> table)
    : m_name(std::move(name)), m_graphId(graphId), m_table(std::move(table))
{
}

bool BaseMaterial::tryGetOffset(ParameterID id, uint32_t &out) const
{
    auto it = m_table.find(id);
    if (it == m_table.end()) return false;
    out = it->second;
    return true;
}

void MaterialManager::init()
{
    if (s_initialized) {
        RP_CORE_WARN("MaterialManager already initialized");
        return;
    }

    s_materials.clear();

    s_materialBuffer = std::make_unique<FreeListStorageBuffer>(sizeof(MaterialData), MAX_MATERIALS,
                                                               DescriptorSetBindingLocation::MATERIAL_DATA_SSBO);
    s_graphBuffer = std::make_unique<VirtualStorageBuffer>(static_cast<VkDeviceSize>(GRAPH_ARENA_BLOCKS) * GRAPH_BLOCK_BYTES,
                                                           GRAPH_BLOCK_BYTES, DescriptorSetBindingLocation::GRAPH_DATA_SSBO);

    s_surfaceGraphManager = std::make_unique<SurfaceGraphManager>();

    auto asset = AssetManager::importDefaultAsset(AssetType::TEXTURE);
    auto defaultTexture = asset ? asset.get()->getUnderlyingAsset<Texture>() : nullptr;
    if (defaultTexture && defaultTexture->isReady()) {
        s_defaultTextureIndex = defaultTexture->getBindlessIndex();
    } else {
        RP_CORE_ERROR("Failed to get default white texture index");
        s_defaultTextureIndex = 0;
    }

    s_initialized = true;
    createDefaultMaterials();
}

void MaterialManager::releaseGraphResources()
{
    s_materials.clear();
    s_surfaceGraphManager.reset();
}

void MaterialManager::shutdown()
{
    s_materials.clear();
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

uint32_t MaterialManager::allocateGraphData(uint32_t sizeBytes)
{
    RP_ASSERT(s_graphBuffer != nullptr, "Initialise the material manager first");
    VkDeviceSize offsetBytes = 0;
    if (!s_graphBuffer->allocate(sizeBytes, offsetBytes)) return UINT32_MAX;
    return static_cast<uint32_t>(offsetBytes / sizeof(uint32_t));
}

void MaterialManager::freeGraphData(uint32_t uintOffset)
{
    RP_ASSERT(s_graphBuffer != nullptr, "Initialise the material manager first");
    s_graphBuffer->free(static_cast<VkDeviceSize>(uintOffset) * sizeof(uint32_t));
}

void MaterialManager::writeGraphData(uint32_t uintOffset, const void *data, uint32_t sizeBytes)
{
    RP_ASSERT(s_graphBuffer != nullptr, "Initialise the material manager first");
    s_graphBuffer->write(static_cast<VkDeviceSize>(uintOffset) * sizeof(uint32_t), data, sizeBytes);
}

void MaterialManager::createDefaultMaterials()
{
    // Blender-style default surface: a bare output node resolves to white albedo, roughness 0.5,
    // metallic 0 and ao 1 from the SurfaceData fallbacks
    MaterialGraph graph;
    graph.name = "Default";
    graph.nodes.push_back({.id = 1, .type = GraphNodeType::SURFACE_OUTPUT});
    graph.outputNodeId = 1;

    uint32_t graphId = s_surfaceGraphManager->registerGraph(graph);
    if (graphId == UINT32_MAX) {
        RP_CORE_ERROR("Failed to compile the default material graph");
        return;
    }

    createMaterial("Default Material", graphId, {});
}

std::shared_ptr<BaseMaterial> MaterialManager::getMaterial(const std::string &name)
{
    auto it = s_materials.find(name);
    return it == s_materials.end() ? nullptr : it->second;
}

std::shared_ptr<BaseMaterial> MaterialManager::createMaterial(const std::string &name, uint32_t graphId,
                                                              std::unordered_map<ParameterID, uint32_t> table)
{
    RP_ASSERT(s_initialized, "Initialise the material manager first");

    if (s_materials.find(name) != s_materials.end()) {
        RP_CORE_ERROR("Material '{}' already exists", name);
        return nullptr;
    }

    auto material = std::make_shared<BaseMaterial>(name, graphId, std::move(table));
    s_materials[name] = material;
    return material;
}

uint32_t MaterialManager::getDefaultTextureIndex()
{
    return s_defaultTextureIndex;
}

void MaterialManager::printMaterialNames()
{
    for (auto &[name, material] : s_materials) {
        RP_CORE_INFO("\t {}", name);
    }
}

} // namespace Rapture

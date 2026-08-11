#include "Terrain3D.h"

#include "components/TerrainComponent.h"
#include "scenes/Scene.h"

namespace Rapture {

static constexpr std::string_view KEY_TERRAIN = "terrain";
static constexpr std::string_view KEY_ENABLED = "enabled";
static constexpr std::string_view KEY_CHUNK_WORLD_SIZE = "chunkWorldSize";
static constexpr std::string_view KEY_HEIGHT_SCALE = "heightScale";
static constexpr std::string_view KEY_TERRAIN_WORLD_SIZE = "terrainWorldSize";
static constexpr std::string_view KEY_CHUNK_GRID_SIZE = "chunkGridSize";
static constexpr std::string_view KEY_HEIGHTMAP_TYPE = "heightmapType";

static TerrainConfig s_defaultConfig()
{
    static constexpr float CHUNK_SIZE = 64.0f;
    static constexpr int32_t CHUNK_RADIUS = 10;
    static constexpr uint32_t GRID_SIDE = 2 * CHUNK_RADIUS + 1;

    TerrainConfig config;
    config.chunkWorldSize = CHUNK_SIZE;
    config.heightScale = 70.0f;
    config.terrainWorldSize = CHUNK_SIZE * static_cast<float>(GRID_SIDE);
    config.chunkGridSize = GRID_SIDE * GRID_SIDE;
    config.hmType = HM_CEPV;
    return config;
}

Terrain3D::Terrain3D(Scene &scene, std::string_view name) : Node3D(scene, name)
{
    m_entity.set<TerrainComponent>(s_defaultConfig()).isEnabled = true;
}

const TypeInfo &Terrain3D::staticType()
{
    static const TypeInfo type("Terrain3D", &Node3D::staticType());
    return type;
}

const TypeInfo &Terrain3D::type() const
{
    return staticType();
}

const TerrainConfig &Terrain3D::config() const
{
    static const TerrainConfig s_empty;

    const auto *terrain = m_entity.tryRead<TerrainComponent>();
    if (terrain == nullptr || terrain->generator == nullptr) {
        return s_empty;
    }
    return terrain->generator->getConfig();
}

void Terrain3D::setConfig(const TerrainConfig &config)
{
    // rebuilding runs the whole generator init again, so an unchanged config keeps the one already built
    if (m_entity.has<TerrainComponent>() && this->config() == config) {
        return;
    }

    bool enabled = isEnabled();
    m_entity.set<TerrainComponent>(config).isEnabled = enabled;
}

bool Terrain3D::isEnabled() const
{
    const auto *terrain = m_entity.tryRead<TerrainComponent>();
    return terrain != nullptr && terrain->isEnabled;
}

void Terrain3D::setEnabled(bool enabled)
{
    if (!m_entity.has<TerrainComponent>()) {
        return;
    }
    auto terrain = m_entity.write<TerrainComponent>();
    terrain->isEnabled = enabled;
}

void Terrain3D::serialize(WriteNode node) const
{
    Node3D::serialize(node);

    const TerrainConfig &current = config();

    WriteNode terrain = node.addObject(KEY_TERRAIN);
    terrain.set(KEY_ENABLED, isEnabled());
    terrain.set(KEY_CHUNK_WORLD_SIZE, static_cast<double>(current.chunkWorldSize));
    terrain.set(KEY_HEIGHT_SCALE, static_cast<double>(current.heightScale));
    terrain.set(KEY_TERRAIN_WORLD_SIZE, static_cast<double>(current.terrainWorldSize));
    terrain.set(KEY_CHUNK_GRID_SIZE, static_cast<uint64_t>(current.chunkGridSize));
    terrain.set(KEY_HEIGHTMAP_TYPE, static_cast<uint64_t>(current.hmType));
}

void Terrain3D::deserialize(ReadNode node)
{
    Node3D::deserialize(node);

    ReadNode terrain = node.child(KEY_TERRAIN);
    if (!terrain.valid()) {
        return;
    }

    TerrainConfig loaded = config();
    loaded.chunkWorldSize = static_cast<float>(terrain.child(KEY_CHUNK_WORLD_SIZE).asF64(loaded.chunkWorldSize));
    loaded.heightScale = static_cast<float>(terrain.child(KEY_HEIGHT_SCALE).asF64(loaded.heightScale));
    loaded.terrainWorldSize = static_cast<float>(terrain.child(KEY_TERRAIN_WORLD_SIZE).asF64(loaded.terrainWorldSize));
    loaded.chunkGridSize = static_cast<uint32_t>(terrain.child(KEY_CHUNK_GRID_SIZE).asU64(loaded.chunkGridSize));
    loaded.hmType = static_cast<HeightmapType>(terrain.child(KEY_HEIGHTMAP_TYPE).asU64(loaded.hmType));

    setConfig(loaded);
    setEnabled(terrain.child(KEY_ENABLED).asBool(true));
}

} // namespace Rapture

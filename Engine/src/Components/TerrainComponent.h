#ifndef RAPTURE__TERRAINCOMPONENT_H
#define RAPTURE__TERRAINCOMPONENT_H

#include "Generators/Terrain/TerrainGenerator.h"

#include <memory>

namespace Rapture {

/**
 * @brief Component that attaches terrain generation to an entity.
 *
 * The terrain entity's TransformComponent can be used for terrain-level
 * transformations (though typically terrain is at origin). The TerrainGenerator
 * handles all chunk management, LOD selection, and GPU resources.
 *
 * Usage:
 *   auto terrainEntity = scene->createEntity("Terrain");
 *   auto& terrainComp = terrainEntity.addComponent<TerrainComponent>();
 *   terrainComp.generator.init({.chunkWorldSize = 64.0f, .heightScale = 100.0f});
 *   terrainComp.generator.setHeightmap(heightmapTexture);
 *   terrainComp.generator.loadChunksAroundPosition(cameraPos, 3);
 */
struct TerrainComponent {
    TerrainGenerator generator;
    bool isEnabled = true;

    TerrainComponent() = default;

    // Initialize with config
    explicit TerrainComponent(const TerrainConfig &config) { generator.init(config); }

    // Initialize with config and heightmap
    TerrainComponent(const TerrainConfig &config, std::shared_ptr<Texture> heightmap)
    {
        generator.init(config);
        generator.setHeightmap(heightmap);
    }

    ~TerrainComponent() { generator.shutdown(); }
};

} // namespace Rapture

#endif // RAPTURE__TERRAINCOMPONENT_H

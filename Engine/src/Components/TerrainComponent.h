#ifndef RAPTURE__TERRAINCOMPONENT_H
#define RAPTURE__TERRAINCOMPONENT_H

#include "Generators/Terrain/TerrainGenerator.h"

#include <memory>

namespace Rapture {

struct TerrainComponent {
    std::unique_ptr<TerrainGenerator> generator = nullptr;
    bool isEnabled = false;
    bool renderChunkBounds = false;
    int32_t forcedLOD = -1;

    TerrainComponent()
    {
        generator = std::make_unique<TerrainGenerator>();
        generator->init({});
        generator->generateDefaultNoiseTextures();
    }

    TerrainComponent(const TerrainConfig &config)
    {
        (void)config;
        generator = std::make_unique<TerrainGenerator>();
        generator->init(config);
        generator->generateDefaultNoiseTextures();
    }
};

} // namespace Rapture

#endif // RAPTURE__TERRAINCOMPONENT_H

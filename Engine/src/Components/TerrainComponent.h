#ifndef RAPTURE__TERRAINCOMPONENT_H
#define RAPTURE__TERRAINCOMPONENT_H

#include "Generators/Terrain/TerrainGenerator.h"

#include <memory>

namespace Rapture {

struct TerrainComponent {
    TerrainGenerator generator;
    bool isEnabled = true;

    TerrainComponent() = default;

    explicit TerrainComponent(const TerrainConfig &config) { generator.init(config); }

    ~TerrainComponent() { generator.shutdown(); }
};

} // namespace Rapture

#endif // RAPTURE__TERRAINCOMPONENT_H

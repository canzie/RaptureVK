#ifndef RAPTURE__TERRAIN3D_H
#define RAPTURE__TERRAIN3D_H

#include "generators/terrain/TerrainTypes.h"
#include "scenes/instances/Node3D.h"

namespace Rapture {

/**
 * @brief A generated heightfield, streamed in chunks around the camera.
 */
class Terrain3D : public Node3D {
  public:
    Terrain3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    const TerrainConfig &config() const;

    /**
     * @brief Rebuilds the terrain from a new configuration
     * @param config The grid and heightmap settings to generate from
     */
    void setConfig(const TerrainConfig &config);

    bool isEnabled() const;
    void setEnabled(bool enabled);

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;
};

} // namespace Rapture

#endif // RAPTURE__TERRAIN3D_H

#ifndef RAPTURE__AWORLD_H
#define RAPTURE__AWORLD_H

#include "assets/asset_manager/Asset.h"
#include "scene/World.h"

#include <memory>

namespace Rapture {

/**
 * @brief A world asset, held as the scene and the settings it is played with
 */
class AWorld : public Asset {
  public:
    explicit AWorld(std::unique_ptr<World> world);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    std::vector<uint8_t> serialize() const override;

    World &world() { return *m_world; }
    const World &world() const { return *m_world; }

    World *operator->() const { return m_world.get(); }

  private:
    std::unique_ptr<World> m_world;
};

} // namespace Rapture

#endif // RAPTURE__AWORLD_H

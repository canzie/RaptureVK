#ifndef RAPTURE__WORLD_H
#define RAPTURE__WORLD_H

#include "asset_manager/AssetCommon.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Rapture {

class Scene;

/**
 * @brief How a world is played.
 */
struct WorldData {
    AssetHandle puppet = INVALID_ASSET_HANDLE;
    AssetHandle controller = INVALID_ASSET_HANDLE;
};

/**
 * @brief One playable space.
 */
class World {
  public:
    explicit World(std::string name);
    ~World();

    World(const World &) = delete;
    World &operator=(const World &) = delete;

    Scene *getScene() const { return m_scene.get(); }

    WorldData &data() { return m_data; }
    const WorldData &data() const { return m_data; }

    void onUpdate(float dt);

    bool isActive() const { return m_isActive; }
    void setActive(bool active) { m_isActive = active; }

    const std::string &getName() const { return m_name; }

    /**
     * @brief Serializes this world into a self-contained blob
     * @return The serialized bytes, empty if the document could not be written
     */
    std::vector<uint8_t> serialize() const;

    /**
     * @brief Rebuilds a world from a blob produced by serialize
     * @param blob The serialized bytes
     * @return The world, or nullptr if the blob does not hold a readable document
     */
    static std::unique_ptr<World> deserialize(std::span<const uint8_t> blob);

  private:
    World(std::string name, std::unique_ptr<Scene> scene);

    std::string m_name;
    std::unique_ptr<Scene> m_scene;
    WorldData m_data;
    bool m_isActive = false;
};

} // namespace Rapture

#endif // RAPTURE__WORLD_H

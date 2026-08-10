#ifndef RAPTURE__WORLD_H
#define RAPTURE__WORLD_H

#include "asset_manager/AssetCommon.h"
#include "input/ControlInput.h"
#include "serialization/SerialDocument.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Rapture {

class Controller;
class Scene;

/**
 * @brief How a world is played.
 */
struct WorldData {
    AssetHandle puppet = INVALID_ASSET_HANDLE;
    AssetHandle controller = INVALID_ASSET_HANDLE;
};

/**
 * @brief Whether a world is being played, and whether it advances while it is.
 */
enum class PlayState {
    STOPPED,
    PLAYING,
    PAUSED
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

    /**
     * @brief Starts advancing this world's scene
     */
    void play();

    /**
     * @brief Holds the scene where it is
     */
    void pause();

    /**
     * @brief Advances the scene again from where pausing left it
     */
    void resume();

    /**
     * @brief Ends the run and rewinds the scene to how it was when the run started
     */
    void stop();

    PlayState playState() const { return m_playState; }

    /**
     * @brief Hands the run the intent its controller drives from on the next update
     * @param intent Device-agnostic input for this frame
     */
    void setIntent(const ControlInput &intent) { m_intent = intent; }

    /**
     * @brief The controller this world's run is driven by
     * @return The controller, or nullptr if the world is not being played
     */
    Controller *playController() const { return m_playController.get(); }

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

    /// What the scene is rewound to on stop, held only for as long as a run is up
    SerialDocument m_snapshot;
    std::unique_ptr<Controller> m_playController;
    ControlInput m_intent;
    PlayState m_playState = PlayState::STOPPED;
    bool m_isActive = false;
};

} // namespace Rapture

#endif // RAPTURE__WORLD_H

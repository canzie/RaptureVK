#ifndef RAPTURE__SCENE_MANAGER_H
#define RAPTURE__SCENE_MANAGER_H

#include "Scene.h"
#include "World.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define RAPTURE_DEFAULT_SCENE_NAME "DefaultScene"

namespace Rapture {

class SceneManager {
  public:
    SceneManager() = default;
    ~SceneManager() = default;

    SceneManager(const SceneManager &) = delete;
    SceneManager &operator=(const SceneManager &) = delete;

    Scene *createScene(const std::string &name);
    void destroyScene(const std::string &name);
    Scene *getScene(const std::string &name);

    const std::vector<Scene *> &getActiveScenes() const { return m_activeScenes; }

    bool isSceneActive(Scene *scene) const;

    void activateScene(const std::string &name);
    void activateScene(Scene *scene);
    void deactivateScene(const std::string &name);
    void deactivateScene(Scene *scene);

    World *createWorld(const std::string &worldName);
    void destroyWorld(const std::string &worldName);
    World *getWorld(const std::string &worldName);

    /**
     * @brief Activate a world and its main scene.
     * @param worldName Name of the world to activate.
     */
    void setActiveWorld(const std::string &worldName);
    World *getActiveWorld() const { return m_activeWorld; }

    void reset();

  private:
    std::unordered_map<std::string, std::unique_ptr<Scene>> m_scenes;
    std::vector<Scene *> m_activeScenes;

    std::unordered_map<std::string, std::unique_ptr<World>> m_worlds;
    World *m_activeWorld = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__SCENE_MANAGER_H

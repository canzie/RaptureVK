#ifndef RAPTURE__SCENE_MANAGER_H
#define RAPTURE__SCENE_MANAGER_H

#include "Scene.h"
#include "World.h"
#include <filesystem>
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

    /**
     * @brief Builds a live scene from a scene asset, or returns the one already open for it
     * @param handle The scene asset to open
     * @return The live scene, or nullptr if the asset could not be read
     */
    Scene *openScene(AssetHandle handle);

    /**
     * @brief Writes a scene back into the asset it came from, importing a new one if it has none
     * @param scene The scene to save
     * @param outputFolder Directory a scene with no asset yet is written into
     * @return True if the scene's asset now holds its contents
     */
    bool saveScene(Scene &scene, const std::filesystem::path &outputFolder);

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

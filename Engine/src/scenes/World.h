#pragma once
#include "Scene.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Rapture {

class World {
  public:
    World(const std::string &name) : m_name(name), m_isActive(false) {}

    ~World()
    {
        // Clear all scenes to ensure proper cleanup
        m_scenes.clear();
    }

    // World operations
    void initialize()
    {
        // Initialize world resources
    }

    void shutdown()
    {
        // Shutdown world resources
        m_scenes.clear();
        m_isActive = false;
    }

    void update(float deltaTime)
    {
        // Update all active scenes
        for (auto &[name, scene] : m_scenes) {
            scene->onUpdate(deltaTime);
        }
    }

    // Scene management within a world (scenes are owned by SceneManager, World only references them)
    void addScene(const std::string &sceneName, Scene *scene) { m_scenes[sceneName] = scene; }

    void removeScene(const std::string &sceneName) { m_scenes.erase(sceneName); }

    Scene *getScene(const std::string &sceneName)
    {
        auto it = m_scenes.find(sceneName);
        if (it != m_scenes.end()) {
            return it->second;
        }
        return nullptr;
    }

    std::vector<std::string> getSceneNames() const
    {
        std::vector<std::string> names;
        for (auto &[name, _] : m_scenes) {
            names.push_back(name);
        }
        return names;
    }

    // Set the main scene of this world
    void setMainScene(const std::string &sceneName)
    {
        auto it = m_scenes.find(sceneName);
        if (it != m_scenes.end()) {
            m_mainScene = it->second;
            m_mainSceneName = sceneName;
        }
    }

    Scene *getMainScene() const { return m_mainScene; }

    // World state
    bool isActive() const { return m_isActive; }
    void setActive(bool active) { m_isActive = active; }
    const std::string &getName() const { return m_name; }

  private:
    std::string m_name;
    bool m_isActive = false;
    std::unordered_map<std::string, Scene *> m_scenes;
    Scene *m_mainScene = nullptr;
    std::string m_mainSceneName;
};

} // namespace Rapture

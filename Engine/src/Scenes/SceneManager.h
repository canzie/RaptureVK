#pragma once
#include "Events/GameEvents.h"
#include "Logging/Log.h"
#include "Scene.h"
#include "World.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Rapture {

class SceneManager {
  public:
    // Singleton pattern
    static SceneManager &getInstance()
    {
        static SceneManager instance;
        return instance;
    }

    // Scene operations
    std::shared_ptr<Scene> createScene(const std::string &name)
    {
        auto scene = std::make_shared<Scene>();
        m_scenes[name] = scene;
        return scene;
    }

    void destroyScene(const std::string &name)
    {
        if (m_activeScene == m_scenes[name]) {
            m_activeScene = nullptr;
        }
        m_scenes.erase(name);
    }

    std::shared_ptr<Scene> getScene(const std::string &name)
    {
        auto it = m_scenes.find(name);
        if (it != m_scenes.end()) {
            return it->second;
        }
        return nullptr;
    }

    std::shared_ptr<Scene> getActiveScene() { return m_activeScene; }

    // Activation
    void setActiveScene(const std::string &name)
    {
        RP_CORE_INFO("Setting active scene to: {0}", name);

        auto it = m_scenes.find(name);
        if (it != m_scenes.end()) {
            std::shared_ptr<Scene> oldScene = m_activeScene;
            m_activeScene = it->second;

            // Notify listeners about scene deactivation
            if (oldScene) {
                GameEvents::onSceneDeactivated().publish(oldScene);
            }

            // Notify listeners about scene activation
            GameEvents::onSceneActivated().publish(m_activeScene);
        }
    }

    void setActiveScene(std::shared_ptr<Scene> scene)
    {
        // Find the scene name first
        std::string sceneName;
        for (auto &[name, s] : m_scenes) {
            if (s == scene) {
                sceneName = name;
                break;
            }
        }

        if (!sceneName.empty()) {
            setActiveScene(sceneName);
        }
    }

    // World interactions
    void registerWorld(std::shared_ptr<World> world) { m_worlds[world->getName()] = world; }

    void unregisterWorld(const std::string &worldName) { m_worlds.erase(worldName); }

    std::shared_ptr<World> getWorld(const std::string &worldName)
    {
        auto it = m_worlds.find(worldName);
        if (it != m_worlds.end()) {
            return it->second;
        }
        return nullptr;
    }

    // Set active world and its main scene as active scene
    void setActiveWorld(const std::string &worldName)
    {
        auto world = getWorld(worldName);
        if (world) {
            if (m_activeWorld) {
                m_activeWorld->setActive(false);
            }

            m_activeWorld = world;
            m_activeWorld->setActive(true);

            // Set this world's main scene as the active scene
            if (auto mainScene = world->getMainScene()) {
                setActiveScene(mainScene);
            }

            // Notify listeners
            GameEvents::onWorldActivated().publish(m_activeWorld);
        }
    }

    std::shared_ptr<World> getActiveWorld() { return m_activeWorld; }

    void reset()
    {
        m_scenes.clear();
        m_worlds.clear();
        m_activeScene.reset();
        m_activeWorld.reset();
    }

  private:
    SceneManager() = default;
    ~SceneManager() = default;

    std::unordered_map<std::string, std::shared_ptr<Scene>> m_scenes;
    std::shared_ptr<Scene> m_activeScene;

    std::unordered_map<std::string, std::shared_ptr<World>> m_worlds;
    std::shared_ptr<World> m_activeWorld;
};

} // namespace Rapture
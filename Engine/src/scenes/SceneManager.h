#pragma once
#include "Scene.h"
#include "World.h"
#include "events/GameEvents.h"
#include "logging/Log.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Rapture {

class SceneManager {
  public:
    SceneManager() = default;
    ~SceneManager() = default;

    SceneManager(const SceneManager &) = delete;
    SceneManager &operator=(const SceneManager &) = delete;

    // Scene operations
    Scene *createScene(const std::string &name)
    {
        auto scene = std::make_unique<Scene>();
        Scene *scenePtr = scene.get();
        m_scenes[name] = std::move(scene);
        return scenePtr;
    }

    void destroyScene(const std::string &name)
    {
        auto it = m_scenes.find(name);
        if (it == m_scenes.end()) {
            return;
        }
        if (m_activeScene == it->second.get()) {
            m_activeScene = nullptr;
        }
        m_scenes.erase(it);
    }

    Scene *getScene(const std::string &name)
    {
        auto it = m_scenes.find(name);
        if (it != m_scenes.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    Scene *getActiveScene() const { return m_activeScene; }

    // Activation
    void setActiveScene(const std::string &name)
    {
        RP_CORE_INFO("Setting active scene to: {0}", name);

        auto it = m_scenes.find(name);
        if (it != m_scenes.end()) {
            Scene *oldScene = m_activeScene;
            m_activeScene = it->second.get();

            // Notify listeners about scene deactivation
            if (oldScene != nullptr) {
                GameEvents::onSceneDeactivated().publish(*oldScene);
            }

            // Notify listeners about scene activation
            GameEvents::onSceneActivated().publish(*m_activeScene);
        }
    }

    void setActiveScene(Scene *scene)
    {
        // Find the scene name first
        std::string sceneName;
        for (auto &[name, s] : m_scenes) {
            if (s.get() == scene) {
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

    std::shared_ptr<World> getActiveWorld() const { return m_activeWorld; }

    void reset()
    {
        m_activeScene = nullptr;
        m_activeWorld.reset();
        m_worlds.clear();
        m_scenes.clear();
    }

  private:
    std::unordered_map<std::string, std::unique_ptr<Scene>> m_scenes;
    Scene *m_activeScene = nullptr;

    std::unordered_map<std::string, std::shared_ptr<World>> m_worlds;
    std::shared_ptr<World> m_activeWorld;
};

} // namespace Rapture

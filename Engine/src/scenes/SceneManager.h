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
    World *createWorld(const std::string &worldName)
    {
        auto world = std::make_unique<World>(worldName);
        World *worldPtr = world.get();
        m_worlds[worldName] = std::move(world);
        return worldPtr;
    }

    void destroyWorld(const std::string &worldName)
    {
        auto it = m_worlds.find(worldName);
        if (it == m_worlds.end()) {
            return;
        }
        if (m_activeWorld == it->second.get()) {
            m_activeWorld = nullptr;
        }
        m_worlds.erase(it);
    }

    World *getWorld(const std::string &worldName)
    {
        auto it = m_worlds.find(worldName);
        if (it != m_worlds.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    // Set active world and its main scene as active scene
    void setActiveWorld(const std::string &worldName)
    {
        World *world = getWorld(worldName);
        if (world != nullptr) {
            if (m_activeWorld != nullptr) {
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

    World *getActiveWorld() const { return m_activeWorld; }

    void reset()
    {
        m_activeScene = nullptr;
        m_activeWorld = nullptr;
        m_worlds.clear();
        m_scenes.clear();
    }

  private:
    std::unordered_map<std::string, std::unique_ptr<Scene>> m_scenes;
    Scene *m_activeScene = nullptr;

    std::unordered_map<std::string, std::unique_ptr<World>> m_worlds;
    World *m_activeWorld = nullptr;
};

} // namespace Rapture

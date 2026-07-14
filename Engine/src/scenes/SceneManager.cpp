#include "SceneManager.h"

#include "events/GameEvents.h"

#include <algorithm>

namespace Rapture {

Scene *SceneManager::createScene(const std::string &name)
{
    auto scene = std::make_unique<Scene>(name);
    Scene *scenePtr = scene.get();
    m_scenes[name] = std::move(scene);
    return scenePtr;
}

void SceneManager::destroyScene(const std::string &name)
{
    auto it = m_scenes.find(name);
    if (it == m_scenes.end()) {
        return;
    }
    deactivateScene(it->second.get());
    m_scenes.erase(it);
}

Scene *SceneManager::getScene(const std::string &name)
{
    auto it = m_scenes.find(name);
    if (it != m_scenes.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool SceneManager::isSceneActive(Scene *scene) const
{
    return std::find(m_activeScenes.begin(), m_activeScenes.end(), scene) != m_activeScenes.end();
}

void SceneManager::activateScene(const std::string &name)
{
    auto it = m_scenes.find(name);
    if (it != m_scenes.end()) {
        activateScene(it->second.get());
    }
}

void SceneManager::activateScene(Scene *scene)
{
    if (scene == nullptr || isSceneActive(scene)) {
        return;
    }
    m_activeScenes.push_back(scene);
    GameEvents::onSceneActivated().publish(*scene);
}

void SceneManager::deactivateScene(const std::string &name)
{
    auto it = m_scenes.find(name);
    if (it != m_scenes.end()) {
        deactivateScene(it->second.get());
    }
}

void SceneManager::deactivateScene(Scene *scene)
{
    if (scene == nullptr) {
        return;
    }
    auto it = std::find(m_activeScenes.begin(), m_activeScenes.end(), scene);
    if (it == m_activeScenes.end()) {
        return;
    }
    m_activeScenes.erase(it);
    GameEvents::onSceneDeactivated().publish(*scene);
}

World *SceneManager::createWorld(const std::string &worldName)
{
    auto world = std::make_unique<World>(worldName);
    World *worldPtr = world.get();
    m_worlds[worldName] = std::move(world);
    return worldPtr;
}

void SceneManager::destroyWorld(const std::string &worldName)
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

World *SceneManager::getWorld(const std::string &worldName)
{
    auto it = m_worlds.find(worldName);
    if (it != m_worlds.end()) {
        return it->second.get();
    }
    return nullptr;
}

void SceneManager::setActiveWorld(const std::string &worldName)
{
    World *world = getWorld(worldName);
    if (world == nullptr) {
        return;
    }

    if (m_activeWorld != nullptr) {
        m_activeWorld->setActive(false);
    }

    m_activeWorld = world;
    m_activeWorld->setActive(true);

    if (auto *mainScene = world->getMainScene()) {
        activateScene(mainScene);
    }

    GameEvents::onWorldActivated().publish(m_activeWorld);
}

void SceneManager::reset()
{
    m_activeScenes.clear();
    m_activeWorld = nullptr;
    m_worlds.clear();
    m_scenes.clear();
}

} // namespace Rapture

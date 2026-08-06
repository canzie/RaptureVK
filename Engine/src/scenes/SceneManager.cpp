#include "SceneManager.h"

#include "asset_manager/AssetManager.h"
#include "events/GameEvents.h"
#include "logging/Log.h"
#include "scenes/SceneAsset.h"

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

Scene *SceneManager::openScene(AssetHandle handle)
{
    if (handle == INVALID_ASSET_HANDLE) {
        return nullptr;
    }

    for (const auto &[name, scene] : m_scenes) {
        if (scene->sourceAsset() == handle) {
            return scene.get();
        }
    }

    AssetRef ref = AssetManager::getAsset(handle);
    const SceneAsset *sceneAsset = ref ? ref.get()->getUnderlyingAsset<SceneAsset>() : nullptr;
    if (sceneAsset == nullptr) {
        RP_CORE_ERROR("asset {} is not a scene", handle);
        return nullptr;
    }

    std::unique_ptr<Scene> scene = Scene::deserialize(sceneAsset->root());
    if (scene == nullptr) {
        return nullptr;
    }

    scene->setSourceAsset(handle);

    Scene *scenePtr = scene.get();
    m_scenes[scene->getSceneName()] = std::move(scene);
    return scenePtr;
}

bool SceneManager::saveScene(Scene &scene, const std::filesystem::path &outputFolder)
{
    auto sceneAsset = std::make_unique<SceneAsset>(scene);

    if (scene.sourceAsset() != INVALID_ASSET_HANDLE) {
        return AssetManager::updateAsset(scene.sourceAsset(), std::move(sceneAsset));
    }

    AssetRef ref = AssetManager::importAsset(AssetImportDataRequest{
        .data = SceneImportData{std::move(sceneAsset)}, .output = outputFolder, .name = scene.getSceneName()});

    if (!ref) {
        RP_CORE_ERROR("Failed to save scene '{}'", scene.getSceneName());
        return false;
    }

    scene.setSourceAsset(ref.get()->getHandle());
    return true;
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

#include "World.h"

#include "assets/asset_manager/Asset.h"
#include "assets/asset_manager/AssetManager.h"
#include "core/serialization/SerialDocument.h"
#include "core/utils/Log.h"
#include "physics/PhysicsSystem.h"
#include "scene/Scene.h"
#include "scene/instances/SceneObject.h"
#include "scene/instances/controllers/Controller.h"

namespace Rapture {

static constexpr std::string_view KEY_NAME = "name";
static constexpr std::string_view KEY_SCENE = "scene";
static constexpr std::string_view KEY_PUPPET = "puppet";
static constexpr std::string_view KEY_CONTROLLER = "controller";
static constexpr std::string_view KEY_GRAVITY = "gravity";

/**
 * @brief Reads a scene object asset into a scene as its own objects
 * @param handle The asset to spawn
 * @param scene The scene the root is parented into
 * @return The spawned root, or nullptr if the asset holds nothing readable
 */
static SceneObject *s_spawnSceneObject(AssetHandle handle, Scene &scene)
{
    AssetRef ref = AssetManager::getAsset(handle);
    Asset *asset = ref.get();
    SerialDocument *document = asset != nullptr ? asset->getUnderlyingAsset<SerialDocument>() : nullptr;
    if (document == nullptr) {
        return nullptr;
    }

    return SceneObject::spawnSubtree(*scene.root(), document->rootView());
}

World::World(std::string name) : m_name(std::move(name)), m_scene(std::make_unique<Scene>(m_name)) {}

World::World(std::string name, std::unique_ptr<Scene> scene) : m_name(std::move(name)), m_scene(std::move(scene)) {}

World::~World() = default;

void World::onUpdate(float dt)
{
    if (!m_isActive) {
        return;
    }

    // driven and simulated before the scene's own update, so what this frame draws is where it left things
    if (m_playState == PlayState::PLAYING) {
        if (m_playController != nullptr) {
            m_playController->setIntent(m_intent);
        }

        m_scene->runTickPhase(TICK_INPUT, dt);
        m_scene->runTickPhase(TICK_PRE_PHYSICS, dt);
        m_scene->stepPhysics(dt);
        m_scene->runTickPhase(TICK_POST_PHYSICS, dt);
    }

    m_scene->onUpdate(dt);
}

void World::setGravity(const glm::vec3 &gravity)
{
    m_data.gravity = gravity;
    applyGravity();
}

void World::applyGravity()
{
    PhysicsSystem *physicsSystem = m_scene != nullptr ? m_scene->physicsSystem() : nullptr;
    if (physicsSystem == nullptr) {
        return;
    }

    physicsSystem->setGravity(m_data.gravity);
}

void World::play()
{
    if (m_playState != PlayState::STOPPED) {
        RP_CORE_WARN("'{}' is already being played", m_name);
        return;
    }

    applyGravity();

    // taken before anything is spawned, so the rewind on stop is what clears the run
    m_snapshot = m_scene->snapshot();

    SceneObject *puppetRoot = s_spawnSceneObject(m_data.puppet, *m_scene);
    if (puppetRoot == nullptr) {
        RP_CORE_WARN("no puppet to be played with, so nothing will be spawned");
    }

    SceneObject *spawnedController = s_spawnSceneObject(m_data.controller, *m_scene);
    m_playController = spawnedController != nullptr ? spawnedController->as<Controller>() : nullptr;
    if (m_playController == nullptr) {
        RP_CORE_WARN("no controller to be played with, so nothing will drive the run");
    } else {
        m_scene->setActiveController(m_playController);

        if (puppetRoot != nullptr) {
            m_playController->possess(puppetRoot);
        }
    }

    m_scene->beginSimulation();

    m_playState = PlayState::PLAYING;
}

void World::pause()
{
    if (m_playState != PlayState::PLAYING) {
        RP_CORE_WARN("'{}' is not playing, so there is nothing to pause", m_name);
        return;
    }

    m_playState = PlayState::PAUSED;
}

void World::resume()
{
    if (m_playState != PlayState::PAUSED) {
        RP_CORE_WARN("'{}' is not paused", m_name);
        return;
    }

    m_playState = PlayState::PLAYING;
}

void World::stop()
{
    if (m_playState == PlayState::STOPPED) {
        return;
    }

    m_playState = PlayState::STOPPED;

    // released before the rewind, which destroys the very instances it is holding on to
    if (m_playController != nullptr) {
        m_playController->unpossess();
        if (m_scene->activeController() == m_playController) {
            m_scene->setActiveController(nullptr);
        }
        m_playController = nullptr;
    }
    m_intent = ControlInput{};

    // closed before the rewind, which destroys the instances its connections are holding on to
    m_scene->endSimulation();

    // the snapshot outlives the rewind, its document is what the scene is read back out of
    m_scene->restoreFrom(m_snapshot.rootView());
    m_snapshot = SerialDocument{};
}

std::vector<uint8_t> World::serialize() const
{
    SerialDocument document;
    WriteNode root = document.root();

    root.set(KEY_NAME, std::string_view(m_name));
    root.set(KEY_PUPPET, m_data.puppet);
    root.set(KEY_CONTROLLER, m_data.controller);

    WriteNode gravity = root.addArray(KEY_GRAVITY);
    gravity.append(m_data.gravity.x);
    gravity.append(m_data.gravity.y);
    gravity.append(m_data.gravity.z);

    m_scene->serialize(root.addObject(KEY_SCENE));

    std::string text = document.toText();
    if (text.empty()) {
        RP_CORE_ERROR("World '{}' could not be written", m_name);
        return {};
    }

    return std::vector<uint8_t>(text.begin(), text.end());
}

std::unique_ptr<World> World::deserialize(std::span<const uint8_t> blob)
{
    std::string_view text(reinterpret_cast<const char *>(blob.data()), blob.size());

    SerialDocument document = SerialDocument::parse(text);
    ReadNode root = document.rootView();
    if (!root.valid()) {
        RP_CORE_ERROR("world blob does not hold a readable document");
        return nullptr;
    }

    std::unique_ptr<Scene> scene = Scene::deserialize(root.child(KEY_SCENE));
    if (scene == nullptr) {
        return nullptr;
    }

    std::unique_ptr<World> world(new World(std::string(root.child(KEY_NAME).asString("")), std::move(scene)));
    world->m_data.puppet = root.child(KEY_PUPPET).asU64(INVALID_ASSET_HANDLE);
    world->m_data.controller = root.child(KEY_CONTROLLER).asU64(INVALID_ASSET_HANDLE);

    ReadNode gravity = root.child(KEY_GRAVITY);
    if (gravity.size() == 3) {
        world->m_data.gravity =
            glm::vec3(static_cast<float>(gravity.at(0).asF64(0.0)), static_cast<float>(gravity.at(1).asF64(0.0)),
                      static_cast<float>(gravity.at(2).asF64(0.0)));
    }

    return world;
}

} // namespace Rapture

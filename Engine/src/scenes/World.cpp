#include "World.h"

#include "asset_manager/Asset.h"
#include "asset_manager/AssetManager.h"
#include "logging/Log.h"
#include "modules/controllers/Controller.h"
#include "modules/puppets/Puppet.h"
#include "scenes/Scene.h"
#include "scenes/instances/Instance.h"
#include "serialization/SerialDocument.h"

namespace Rapture {

static constexpr std::string_view KEY_NAME = "name";
static constexpr std::string_view KEY_SCENE = "scene";
static constexpr std::string_view KEY_PUPPET = "puppet";
static constexpr std::string_view KEY_CONTROLLER = "controller";

static ModuleClass *s_moduleOf(const AssetRef &ref)
{
    Asset *asset = ref.get();
    return asset != nullptr ? asset->getUnderlyingAsset<ModuleClass>() : nullptr;
}

static std::unique_ptr<Controller> s_instantiateController(AssetHandle handle)
{
    AssetRef ref = AssetManager::getAsset(handle);
    ModuleClass *source = s_moduleOf(ref);
    if (source == nullptr || !source->isA<Controller>()) {
        RP_CORE_WARN("no controller to be played with, so nothing will drive the run");
        return nullptr;
    }

    // a run drives a copy, so what the controller picks up along the way never lands in the asset
    std::unique_ptr<ModuleClass> module = ModuleClass::fromBlob(source->toBlob());
    if (module == nullptr || !module->isA<Controller>()) {
        RP_CORE_ERROR("'{}' could not be copied for the run", source->type().name);
        return nullptr;
    }

    return std::unique_ptr<Controller>(static_cast<Controller *>(module.release()));
}

static Instance *s_spawnPuppet(AssetHandle handle, Scene &scene)
{
    AssetRef ref = AssetManager::getAsset(handle);
    ModuleClass *module = s_moduleOf(ref);
    Puppet *puppet = module != nullptr ? module->as<Puppet>() : nullptr;
    if (puppet == nullptr) {
        RP_CORE_WARN("no puppet to be played with, so nothing will be spawned");
        return nullptr;
    }

    return puppet->spawn(*scene.root());
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
            m_playController->update(dt, m_intent);
        }
        m_scene->stepPhysics(dt);
    }

    m_scene->onUpdate(dt);
}

void World::play()
{
    if (m_playState != PlayState::STOPPED) {
        RP_CORE_WARN("'{}' is already being played", m_name);
        return;
    }

    m_snapshot = m_scene->snapshot();

    m_playController = s_instantiateController(m_data.controller);
    Instance *puppetRoot = s_spawnPuppet(m_data.puppet, *m_scene);
    if (m_playController != nullptr && puppetRoot != nullptr) {
        m_playController->possess(puppetRoot);
        m_scene->setActiveController(m_playController.get());
    }

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
        if (m_scene->activeController() == m_playController.get()) {
            m_scene->setActiveController(nullptr);
        }
        m_playController.reset();
    }
    m_intent = ControlInput{};

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
    return world;
}

} // namespace Rapture

#include "World.h"

#include "logging/Log.h"
#include "scenes/Scene.h"
#include "serialization/SerialDocument.h"

namespace Rapture {

static constexpr std::string_view KEY_NAME = "name";
static constexpr std::string_view KEY_SCENE = "scene";
static constexpr std::string_view KEY_PUPPET = "puppet";
static constexpr std::string_view KEY_CONTROLLER = "controller";

World::World(std::string name) : m_name(std::move(name)), m_scene(std::make_unique<Scene>(m_name)) {}

World::World(std::string name, std::unique_ptr<Scene> scene) : m_name(std::move(name)), m_scene(std::move(scene)) {}

World::~World() = default;

void World::onUpdate(float dt)
{
    if (m_scene != nullptr) {
        m_scene->onUpdate(dt);
    }
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

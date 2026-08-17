#include "Instance.h"

#include "scene/Scene.h"

namespace Rapture {

static constexpr std::string_view KEY_CLASS = "class";
static constexpr std::string_view KEY_ID = "id";
static constexpr std::string_view KEY_NAME = "name";

Instance::Instance(Scene &scene, std::string_view name)
    : m_scene(&scene), m_id(UUIDGenerator::Generate()), m_name(name), m_tickSlot(Scene::INVALID_TICK_SLOT)
{
}

Instance::~Instance()
{
    setTickEnabled(false);
    onDestroy.fire(this);
}

bool Instance::isTickEnabled() const
{
    return m_tickSlot != Scene::INVALID_TICK_SLOT;
}

void Instance::setTickEnabled(bool enabled)
{
    if (isTickEnabled() == enabled || m_scene == nullptr) {
        return;
    }

    if (enabled) {
        m_tickSlot = m_scene->registerTick(this, m_tickPhase);
        return;
    }

    m_scene->unregisterTick(m_tickSlot, m_tickPhase);
    m_tickSlot = Scene::INVALID_TICK_SLOT;
}

void Instance::setTickPhase(TickPhase phase)
{
    if (m_tickPhase == phase) {
        return;
    }

    // the slot is a place in the old phase's list, so a move out and back in is what changes phase
    bool wasEnabled = isTickEnabled();
    setTickEnabled(false);
    m_tickPhase = phase;
    setTickEnabled(wasEnabled);
}

void Instance::onUpdate(float dt)
{
    (void)dt;
}

void Instance::link(const SceneLoadContext &context)
{
    onLink(context);
}

void Instance::ready()
{
    if (m_isReady) {
        return;
    }

    m_isReady = true;
    onReady();
}

void Instance::onLink(const SceneLoadContext &context)
{
    (void)context;
}

void Instance::onReady()
{
}

void Instance::remintId()
{
    m_id = UUIDGenerator::Generate();
}

const TypeInfo &Instance::staticType()
{
    static const TypeInfo type("Instance", nullptr);
    return type;
}

const TypeInfo &Instance::type() const
{
    return staticType();
}

void Instance::setName(std::string_view name)
{
    m_name = name;
}

void Instance::serialize(WriteNode node) const
{
    node.set(KEY_CLASS, type().name);
    node.set(KEY_ID, m_id);
    node.set(KEY_NAME, std::string_view(m_name));
}

void Instance::deserialize(ReadNode node)
{
    // the construction that got us here minted an id, the document's one replaces it
    m_id = node.child(KEY_ID).asU64(m_id);
    setName(node.child(KEY_NAME).asString(m_name));
}

std::string_view Instance::readClassName(ReadNode node)
{
    return node.child(KEY_CLASS).asString();
}

} // namespace Rapture

#include "Puppet.h"

#include "logging/Log.h"
#include "scenes/instances/Instance.h"

#include <vector>

namespace Rapture {

static constexpr std::string_view KEY_SCENE_ROOT = "sceneRoot";

const TypeInfo &Puppet::staticType()
{
    static const TypeInfo type("Puppet", &ModuleClass::staticType());
    return type;
}

const TypeInfo &Puppet::type() const
{
    return staticType();
}

Instance *Puppet::spawn(Instance &parent) const
{
    if (parent.scene() == nullptr) {
        RP_CORE_ERROR("a puppet spawns into a scene, and '{}' is in none", parent.name());
        return nullptr;
    }

    if (!hasSceneRoot()) {
        RP_CORE_WARN("puppet holds nothing to spawn");
        return nullptr;
    }

    std::vector<Instance *> order;
    if (!Instance::loadSubtree(parent, m_sceneRoot.rootView(), order) || order.empty()) {
        RP_CORE_ERROR("puppet scene root could not be read into the scene");
        return nullptr;
    }

    // what spawned is its own object, not the one it was read from
    for (Instance *instance : order) {
        instance->remintId();
    }

    return order.front();
}

void Puppet::capture(const Instance &root)
{
    SerialDocument sceneRoot;
    root.serialize(sceneRoot.root());
    sceneRoot.freeze();

    m_sceneRoot = std::move(sceneRoot);
}

void Puppet::serialize(WriteNode node) const
{
    ModuleClass::serialize(node);

    if (hasSceneRoot()) {
        node.addCopy(KEY_SCENE_ROOT, m_sceneRoot.rootView());
    }
}

void Puppet::deserialize(ReadNode node)
{
    ModuleClass::deserialize(node);

    m_sceneRoot = SerialDocument::copyOf(node.child(KEY_SCENE_ROOT));
}

} // namespace Rapture

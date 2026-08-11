#include "Puppet.h"

#include "logging/Log.h"
#include "scenes/instances/SceneObject.h"

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

SceneObject *Puppet::spawn(SceneObject &parent) const
{
    if (!hasSceneRoot()) {
        RP_CORE_WARN("puppet holds nothing to spawn");
        return nullptr;
    }

    return SceneObject::spawnSubtree(parent, m_sceneRoot.rootView());
}

void Puppet::capture(const SceneObject &root)
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

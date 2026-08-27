#include "ScriptComponent.h"

#include "core/utils/rp_assert.h"
#include "scene/Scene.h"
#include "scene/components/Components.h"
#include "scripting/lua/LuaRuntime.h"

namespace Rapture {

static constexpr std::string_view KEY_SOURCE = "source";
static constexpr std::string_view KEY_EXTERNAL_PATH = "externalPath";

ScriptComponent::ScriptComponent(Scene &scene, std::string_view name) : SceneComponent(scene, name) {}

const TypeInfo &ScriptComponent::staticType()
{
    static const TypeInfo type("ScriptComponent", &SceneComponent::staticType());
    return type;
}

const TypeInfo &ScriptComponent::type() const
{
    return staticType();
}

void ScriptComponent::setSource(std::string_view source)
{
    m_source = source;
    m_externalDirty = false;
}

void ScriptComponent::setExternalPath(std::filesystem::path path)
{
    m_externalPath = std::move(path);
    m_externalDirty = false;
}

void ScriptComponent::onAttach()
{
    ownerEntity().set<ScriptedComponent>();
}

void ScriptComponent::onDetach()
{
    ownerEntity().tryRemove<ScriptedComponent>();
}

void ScriptComponent::onReady()
{
    RP_ASSERT(scene() != nullptr, "script '{}' is ready in no scene", name());

    scripting::LuaRuntime *runtime = scene()->scriptRuntime();
    if (runtime == nullptr) {
        return;
    }

    runtime->runScript(*this);
}

void ScriptComponent::serialize(WriteNode node) const
{
    SceneComponent::serialize(node);

    node.set(KEY_SOURCE, std::string_view(m_source));

    if (!m_externalPath.empty()) {
        node.set(KEY_EXTERNAL_PATH, m_externalPath.string());
    }
}

void ScriptComponent::deserialize(ReadNode node)
{
    SceneComponent::deserialize(node);

    m_source = node.child(KEY_SOURCE).asString();

    std::string_view path = node.child(KEY_EXTERNAL_PATH).asString();
    m_externalPath = path.empty() ? std::filesystem::path() : std::filesystem::path(path);
    m_externalDirty = false;
}

} // namespace Rapture

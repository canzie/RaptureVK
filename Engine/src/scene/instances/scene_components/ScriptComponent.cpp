#include "ScriptComponent.h"

#include "assets/asset_manager/AssetManager.h"
#include "core/utils/Log.h"
#include "core/utils/rp_assert.h"
#include "scene/Scene.h"
#include "scene/components/Components.h"
#include "scripting/lua/LuaRuntime.h"

namespace Rapture {

static constexpr std::string_view KEY_SCRIPT = "script";

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

void ScriptComponent::setScript(AssetHandle script)
{
    if (m_script == script) {
        return;
    }

    Ref<AScript> asset = AssetManager::getAsset<AScript>(script);
    if (!asset) {
        RP_CORE_ERROR("script {} could not be resolved for '{}'", script, name());
        return;
    }

    m_scriptAsset = std::move(asset);
    m_script = script;
}

std::string_view ScriptComponent::source() const
{
    return m_scriptAsset ? m_scriptAsset->source() : std::string_view{};
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

    node.set(KEY_SCRIPT, m_script);
}

void ScriptComponent::deserialize(ReadNode node)
{
    SceneComponent::deserialize(node);

    AssetHandle script = node.child(KEY_SCRIPT).asU64(INVALID_ASSET_HANDLE);
    if (script != INVALID_ASSET_HANDLE) {
        setScript(script);
    }
}

} // namespace Rapture

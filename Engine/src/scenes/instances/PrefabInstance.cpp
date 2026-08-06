#include "PrefabInstance.h"

#include "asset_manager/AssetManager.h"
#include "components/Components.h"
#include "logging/Log.h"
#include "scenes/Scene.h"

namespace Rapture {

static constexpr std::string_view KEY_PREFAB = "prefab";
static constexpr std::string_view KEY_SOURCE = "source";

PrefabInstance::PrefabInstance(Scene &scene, std::string_view name) : Node3D(scene, name)
{
    m_entity.setComponent<PrefabComponent>();
}

const TypeInfo &PrefabInstance::staticType()
{
    static const TypeInfo type("PrefabInstance", &Node3D::staticType());
    return type;
}

const TypeInfo &PrefabInstance::type() const
{
    return staticType();
}

void PrefabInstance::setPrefab(AssetHandle _prefab)
{
    if (m_prefab == _prefab) {
        return;
    }

    AssetRef ref = AssetManager::getAsset(_prefab);
    if (!ref) {
        RP_CORE_ERROR("prefab {} could not be resolved for '{}'", _prefab, name());
        return;
    }

    auto *component = m_entity.tryGetComponent<PrefabComponent>();
    if (component == nullptr) {
        return;
    }

    component->sourcePrefab = AssetPtr<Prefab>(std::move(ref));
    m_prefab = _prefab;
}

void PrefabInstance::serialize(WriteNode node) const
{
    Node3D::serialize(node);

    WriteNode prefab = node.addObject(KEY_PREFAB);
    prefab.set(KEY_SOURCE, this->prefab());
}

void PrefabInstance::deserialize(ReadNode node)
{
    Node3D::deserialize(node);

    ReadNode prefab = node.child(KEY_PREFAB);
    if (!prefab.valid()) {
        return;
    }

    AssetHandle source = prefab.child(KEY_SOURCE).asU64(INVALID_ASSET_HANDLE);
    if (source != INVALID_ASSET_HANDLE) {
        setPrefab(source);
    }
}

} // namespace Rapture

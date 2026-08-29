#include "Module.h"

#include "assets/asset_manager/Asset.h"
#include "assets/asset_manager/AssetManager.h"
#include "core/serialization/SerialDocument.h"
#include "core/utils/Log.h"

namespace Rapture {

static constexpr std::string_view KEY_ASSET = "asset";

Module::Module(Scene &scene, std::string_view name) : Node3D(scene, name) {}

const TypeInfo &Module::staticType()
{
    static const TypeInfo type("Module", &Node3D::staticType());
    return type;
}

const TypeInfo &Module::type() const
{
    return staticType();
}

bool Module::setAssetHandle(AssetHandle handle)
{
    if (m_contentRoot != nullptr) {
        destroyChild(m_contentRoot);
        m_contentRoot = nullptr;
    }

    m_assetHandle = handle;
    if (m_assetHandle == INVALID_ASSET_HANDLE) {
        return false;
    }

    AssetRef ref = AssetManager::getAsset(m_assetHandle);
    Asset *asset = ref.get();
    SerialDocument *document = asset != nullptr ? asset->getUnderlyingAsset<SerialDocument>() : nullptr;
    if (document == nullptr) {
        RP_CORE_ERROR("asset {} holds no module for '{}' to stand for", m_assetHandle, name());
        return false;
    }

    m_contentRoot = spawnSubtree(*this, document->rootView(), InternalMode::ENABLED);
    if (m_contentRoot == nullptr) {
        return false;
    }

    // the asset is what the contents are read back from, so a scene holding this object never writes them itself
    m_contentRoot->setSerialized(false);

    return true;
}

void Module::serialize(WriteNode node) const
{
    Node3D::serialize(node);

    node.set(KEY_ASSET, m_assetHandle);
}

void Module::deserialize(ReadNode node)
{
    Node3D::deserialize(node);

    setAssetHandle(node.child(KEY_ASSET).asU64(INVALID_ASSET_HANDLE));
}

} // namespace Rapture

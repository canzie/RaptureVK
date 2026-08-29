#include "AMesh.h"

#include "assets/asset_manager/AssetManager.h"
#include "core/utils/Log.h"

namespace Rapture {

const TypeInfo &AMesh::staticType()
{
    static const TypeInfo type("AMesh", &Asset::staticType());
    return type;
}

const TypeInfo &AMesh::type() const
{
    return staticType();
}

AMesh::AMesh(std::vector<AssetHandle> materialSlots) : m_materialSlots(std::move(materialSlots))
{
    if (m_materialSlots.empty()) {
        m_materialSlots.push_back(RE_DEFAULT_MATERIAL_INSTANCE);
    }
}

AssetHandle AMesh::materialSlot(uint32_t slot) const
{
    if (slot >= m_materialSlots.size()) {
        RP_CORE_WARN("slot {} is not one of the {} this mesh has, falling back to the default material", slot,
                     m_materialSlots.size());
        return RE_DEFAULT_MATERIAL_INSTANCE;
    }
    return m_materialSlots[slot];
}

void AMesh::setMaterialSlot(uint32_t slot, AssetHandle material)
{
    if (slot >= m_materialSlots.size()) {
        RP_CORE_ERROR("slot {} is not one of the {} this mesh has", slot, m_materialSlots.size());
        return;
    }

    const AssetMetadata &metadata = AssetManager::getAssetMetadata(material);
    if (metadata.assetType != ASSET_MATERIAL_INSTANCE) {
        RP_CORE_ERROR("{} is not a material instance, leaving slot {} as it was", material, slot);
        return;
    }

    m_materialSlots[slot] = material;
}

} // namespace Rapture

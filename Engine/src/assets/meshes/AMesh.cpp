#include "AMesh.h"

#include "assets/asset_manager/AssetManager.h"
#include "core/utils/Log.h"

namespace Rapture {

void AMesh::setDefaultMaterial(AssetHandle material)
{
    const AssetMetadata &metadata = AssetManager::getAssetMetadata(material);
    if (metadata.assetType != ASSET_MATERIAL_INSTANCE) {
        RP_CORE_ERROR("{} is not a material instance, leaving the default material as it was", material);
        return;
    }

    m_defaultMaterial = material;
}

} // namespace Rapture

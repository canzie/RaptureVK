#include "Asset.h"

#include "AssetManager.h"

namespace Rapture {

AssetMetadata AssetMetadata::null{};
const AssetMetadata AssetMetadata::const_null{};

const TypeInfo &Asset::staticType()
{
    static const TypeInfo type("Asset", nullptr);
    return type;
}

const TypeInfo &Asset::type() const
{
    return staticType();
}

void Asset::onLastUseReleased()
{
    AssetManager::requestUnload(m_handle);
}

} // namespace Rapture

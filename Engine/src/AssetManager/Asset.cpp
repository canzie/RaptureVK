#include "Asset.h"

namespace Rapture {

const Asset Asset::const_null{std::monostate(), 0};
Asset Asset::null{std::monostate(), 0};

AssetMetadata AssetMetadata::null{};
const AssetMetadata AssetMetadata::const_null{};

AssetRef::AssetRef(Asset *_asset, uint32_t *_useCount) noexcept : asset(_asset), m_useCount(_useCount)
{
    if (m_useCount) (*m_useCount)++;
}

AssetRef::AssetRef(const AssetRef &other) noexcept : asset(other.asset), m_useCount(other.m_useCount)
{
    if (m_useCount) (*m_useCount)++;
}

AssetRef::AssetRef(AssetRef &&other) noexcept : asset(other.asset), m_useCount(other.m_useCount)
{
    other.asset = nullptr;
    other.m_useCount = nullptr;
}

AssetRef::~AssetRef() noexcept
{
    if (m_useCount) (*m_useCount)--;
}

} // namespace Rapture

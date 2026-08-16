#include "Asset.h"

#include "AssetManager.h"

namespace Rapture {

const Asset Asset::const_null{std::monostate(), 0};
Asset Asset::null{std::monostate(), 0};

AssetMetadata AssetMetadata::null{};
const AssetMetadata AssetMetadata::const_null{};

AssetRef::AssetRef(Asset *_asset, std::atomic<uint32_t> *_useCount) noexcept : asset(_asset), m_useCount(_useCount)
{
    if (m_useCount) m_useCount->fetch_add(1, std::memory_order_relaxed);
}

AssetRef::AssetRef(const AssetRef &other) noexcept : asset(other.asset), m_useCount(other.m_useCount)
{
    if (m_useCount) m_useCount->fetch_add(1, std::memory_order_relaxed);
}

AssetRef::AssetRef(AssetRef &&other) noexcept : asset(other.asset), m_useCount(other.m_useCount)
{
    other.asset = nullptr;
    other.m_useCount = nullptr;
}

AssetRef::~AssetRef() noexcept
{
    releaseRef();
}

void AssetRef::releaseRef() noexcept
{
    if (!m_useCount) {
        return;
    }
    // fetch_sub returns the previous value, so a result of 1 means this was the last reference
    if (m_useCount->fetch_sub(1, std::memory_order_acq_rel) == 1 && asset != nullptr) {
        AssetManager::requestUnload(asset->getHandle());
    }
}

} // namespace Rapture

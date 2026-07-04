#ifndef RAPTURE__ASSET_HANDLE_H
#define RAPTURE__ASSET_HANDLE_H

#include <atomic>
#include <cstdint>
#include <utility>

#include "utils/rp_assert.h"

namespace Rapture {

class Asset;

/**
 * @brief Wrapper for assets so the assetmanager can keep track of the amount of uses
 *
 * I did not want to use a shared_ptr because the asset manager needs to own it, and overwriting the shared_ptr destructor is just a
 * garbage hack.
 */
class AssetRef {
  public:
    AssetRef() noexcept : asset(nullptr), m_useCount(nullptr) {}
    AssetRef(Asset *_asset, std::atomic<uint32_t> *_useCount) noexcept;
    AssetRef(const AssetRef &other) noexcept;
    AssetRef(AssetRef &&other) noexcept;
    ~AssetRef() noexcept;

    bool operator==(const AssetRef &other) const { return asset == other.asset; }
    explicit operator bool() const { return asset != nullptr; }

    Asset *get() const { return asset; }

    AssetRef &operator=(const AssetRef &other) noexcept
    {
        if (this == &other) return *this;

        releaseRef();

        asset = other.asset;
        m_useCount = other.m_useCount;
        if (m_useCount) m_useCount->fetch_add(1, std::memory_order_relaxed);
        return *this;
    }

    AssetRef &operator=(AssetRef &&other) noexcept
    {
        if (this == &other) return *this;

        releaseRef();

        asset = other.asset;
        m_useCount = other.m_useCount;

        other.asset = nullptr;
        other.m_useCount = nullptr;

        return *this;
    }

  private:
    void releaseRef() noexcept;

    Asset *asset;
    std::atomic<uint32_t> *m_useCount;
};

/**
 * @brief Typed handle over an AssetRef, holds the reference alive and exposes the asset as a T pointer
 */
template <typename T> class AssetPtr {
  public:
    AssetPtr() = default;
    AssetPtr(AssetRef ref) noexcept;

    T *get() const noexcept { return m_ptr; }
    T *operator->() const noexcept { return m_ptr; }
    T &operator*() const noexcept { return *m_ptr; }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }

    const AssetRef &ref() const noexcept { return m_ref; }

  private:
    AssetRef m_ref;
    T *m_ptr = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__ASSET_HANDLE_H

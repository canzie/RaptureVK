#ifndef RAPTURE__ASSET_H
#define RAPTURE__ASSET_H

#include "AssetCommon.h"
#include "AssetImportConfig.h"

#include "core/utils/Ref.h"
#include "core/utils/RefCounted.h"
#include "core/utils/TypeInfo.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Rapture {

/**
 * @brief What the asset manager knows about an asset without loading it
 */
struct AssetMetadata {

    AssetMetadata(const AssetMetadata &) = delete;
    AssetMetadata &operator=(const AssetMetadata &) = delete;
    AssetMetadata() = default;
    static AssetMetadata null;
    static const AssetMetadata const_null;

    AssetType assetType = ASSET_NONE;
    AssetStorageType storageType = AssetStorageType::DISK;

    AssetImportConfigVariant importConfig = std::monostate();
    std::string name = "untitled";

    std::optional<AssetProvenance> provenance;
    std::filesystem::path assetPath;

    AssetEvictionPolicy evictionPolicy = AssetEvictionPolicy::EVICT_IMMEDIATE;
    uint64_t sizeHintBytes = 0;

    /// The class a module's root is, so assets can be filtered by class without being loaded
    const TypeInfo *authoredClass = nullptr;

    bool isDiskAsset() const { return storageType == AssetStorageType::DISK; }
    bool isVirtualAsset() const { return storageType == AssetStorageType::VIRTUAL; }
    const std::string &getName() const { return name; }
    std::filesystem::path getSourcePath() const { return provenance ? provenance->sourcePath : std::filesystem::path{}; }

    operator bool() const { return assetType != ASSET_NONE; }
};

/**
 * @brief Base of everything the asset manager holds.
 *
 * The manager owns an asset for its whole life and a Ref counts a user holding one. What an
 * asset is made of is the branch below this, one class per kind of content.
 */
class Asset : public RefCounted {
  public:
    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    AssetHandle handle() const { return m_handle; }

    /**
     * @brief Gives this asset the identity the manager registered it under
     * @param handle The handle
     */
    void setHandle(AssetHandle handle) { m_handle = handle; }

    AssetStatus status() const { return m_status.load(std::memory_order_acquire); }

    /**
     * @brief Records how far this asset has got through being loaded
     * @param status The status
     */
    void setStatus(AssetStatus status) { m_status.store(status, std::memory_order_release); }

    bool isValid() const { return status() != AssetStatus::FAILED; }

    /**
     * @brief Writes this asset into the payload its asset file holds
     * @return The serialized bytes, empty if this asset holds nothing to write
     */
    virtual std::vector<uint8_t> serialize() const = 0;

    void onLastUseReleased() override;

  protected:
    Asset() = default;

  private:
    AssetHandle m_handle = INVALID_ASSET_HANDLE;
    std::atomic<AssetStatus> m_status{AssetStatus::REQUESTED};
};

/**
 * @brief A use of an asset whose class the holder has no stake in
 */
using AssetRef = Ref<Asset>;

} // namespace Rapture

#endif // RAPTURE__ASSET_H

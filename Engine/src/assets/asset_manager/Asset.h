#pragma once

#include "AssetCommon.h"
#include "AssetImportConfig.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

#include "AssetHandle.h"

#include "assets/materials/MaterialInstance.h"
#include "assets/meshes/ASkeletalMesh.h"
#include "assets/meshes/AStaticMesh.h"
#include "assets/skeletons/ASkeleton.h"
#include "scene/World.h"
#include "core/serialization/SerialDocument.h"
#include "gpu/shaders/Shader.h"
#include "gpu/textures/Texture.h"
#include "core/utils/TypeInfo.h"

namespace Rapture {

using AssetVariant = std::variant<std::monostate, std::unique_ptr<Shader>, std::unique_ptr<Texture>,
                                  std::unique_ptr<BaseMaterial>, std::unique_ptr<MaterialInstance>, std::unique_ptr<AStaticMesh>,
                                  std::unique_ptr<ASkeletalMesh>, std::unique_ptr<SerialDocument>, std::unique_ptr<World>,
                                  std::unique_ptr<ASkeleton>>;

template <typename T, typename Variant>
struct IsAssetType;
template <typename T, typename... Us>
struct IsAssetType<T, std::variant<Us...>> : std::bool_constant<(std::is_same_v<std::unique_ptr<T>, Us> || ...)> {};

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

    std::atomic<uint32_t> useCount{0};
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

class Asset {
  public:
    Asset() = delete;

    explicit Asset(AssetHandle _handle) : handle(_handle), m_asset(std::monostate()) {}
    explicit Asset(AssetVariant asset, AssetHandle _handle) : handle(_handle), m_asset(std::move(asset)) {}
    static const Asset const_null;
    static Asset null;

    ~Asset() = default;

    template <typename T>
    T *getUnderlyingAsset() const
    {
        if (std::holds_alternative<std::unique_ptr<T>>(m_asset)) {
            return std::get<std::unique_ptr<T>>(m_asset).get();
        }
        return nullptr;
    }

    bool isValid() const { return !std::holds_alternative<std::monostate>(m_asset) && status != AssetStatus::FAILED; }
    AssetStatus getStatus() const { return status; }
    AssetHandle getHandle() const { return handle; }
    void setAssetVariant(AssetVariant &&asset) { m_asset = std::move(asset); }

    bool operator==(Asset &other) { return handle == other.getHandle(); };
    operator bool() const { return handle != Asset::null.getHandle(); }

  public:
    const AssetHandle handle;
    std::atomic<AssetStatus> status{AssetStatus::REQUESTED};

  private:
    AssetVariant m_asset;
};

template <typename T>
AssetPtr<T>::AssetPtr(AssetRef ref) noexcept : m_ref(std::move(ref)), m_ptr(m_ref ? m_ref.get()->getUnderlyingAsset<T>() : nullptr)
{
    RP_ASSERT(!m_ref || m_ptr != nullptr, "AssetPtr constructed from an asset that is not of type T");
}

template <typename T>
AssetHandle AssetPtr<T>::getHandle() const noexcept
{
    return m_ref ? m_ref.get()->getHandle() : INVALID_ASSET_HANDLE;
}

} // namespace Rapture

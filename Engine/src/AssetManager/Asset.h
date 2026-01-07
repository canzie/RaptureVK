#pragma once

#include "AssetCommon.h"
#include "AssetImportConfig.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>

#include "Loaders/SceneFileCommon.h"
#include "Materials/MaterialInstance.h"
#include "Meshes/Mesh.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"

namespace Rapture {

using AssetVariant = std::variant<std::monostate, std::unique_ptr<Shader>, std::unique_ptr<Texture>,
                                  std::unique_ptr<MaterialInstance>, std::unique_ptr<Mesh>, std::unique_ptr<SceneFileData>>;

struct AssetMetadata {

    AssetMetadata(const AssetMetadata &) = delete;
    AssetMetadata &operator=(const AssetMetadata &) = delete;
    AssetMetadata(AssetMetadata &&) noexcept = default;
    AssetMetadata &operator=(AssetMetadata &&) noexcept = default;
    AssetMetadata() = default;
    static AssetMetadata null;
    static const AssetMetadata const_null;

    AssetType assetType = AssetType::NONE;
    AssetStorageType storageType = AssetStorageType::DISK;

    std::filesystem::path filePath;
    AssetImportConfigVariant importConfig = std::monostate();
    std::string virtualName = "untitled";

    uint32_t useCount = 0;

    bool isDiskAsset() const { return storageType == AssetStorageType::DISK; }
    bool isVirtualAsset() const { return storageType == AssetStorageType::VIRTUAL; }
    const std::string getName()
    {
        if (storageType == AssetStorageType::DISK) {
            return filePath.string();
        } else if (storageType == AssetStorageType::VIRTUAL) {
            return virtualName;
        }
        return "No Name";
    }

    operator bool() const { return assetType != AssetType::NONE; }
};

class Asset {
  public:
    Asset() = delete;

    explicit Asset(AssetHandle _handle) : handle(_handle), m_asset(std::monostate()) {}
    explicit Asset(AssetVariant asset, AssetHandle _handle) : handle(_handle), m_asset(std::move(asset)) {}
    static const Asset const_null;
    static Asset null;

    ~Asset() = default;

    template <typename T> T *getUnderlyingAsset() const
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

/*
 @brief Wrapper for assets so the assetmanager can keep track of the amount of uses

  I did not want to use a shared_ptr because the asset manager needs to own it, and overwriting the shared_ptr destructor is just a
 garbage hack.
*/
class AssetRef {
  public:
    AssetRef() noexcept : asset(nullptr), m_useCount(nullptr) {}
    AssetRef(Asset *_asset, uint32_t *_useCount) noexcept;
    AssetRef(const AssetRef &other) noexcept;
    AssetRef(AssetRef &&other) noexcept;
    ~AssetRef() noexcept;

    bool operator==(const AssetRef &other) const { return asset == other.asset; }
    explicit operator bool() const { return asset != nullptr; }

    Asset *get() const { return asset; }

    AssetRef &operator=(const AssetRef &other) noexcept
    {
        if (this == &other) return *this;

        if (m_useCount) (*m_useCount)--;

        asset = other.asset;
        m_useCount = other.m_useCount;
        if (m_useCount) (*m_useCount)++;
        return *this;
    }

    AssetRef &operator=(AssetRef &&other) noexcept
    {
        if (this == &other) return *this;
        if (m_useCount) (*m_useCount)--;

        asset = other.asset;
        m_useCount = other.m_useCount;

        other.asset = nullptr;
        other.m_useCount = nullptr;

        return *this;
    }

  private:
    Asset *asset;
    uint32_t *m_useCount;
};

} // namespace Rapture

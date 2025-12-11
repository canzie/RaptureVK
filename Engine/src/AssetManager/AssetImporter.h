#pragma once

#include "Asset.h"

#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <variant>

#include "AssetLoadRequests.h"
#include "Logging/Log.h"
#include <atomic>
#include <mutex>
#include <thread>

#include "AssetHelpers.h"

namespace Rapture {

using AssetImporterFunction = std::function<std::shared_ptr<Asset>(AssetHandle, const AssetMetadata &)>;
static std::map<AssetType, AssetImporterFunction> s_assetImporters;

class AssetImporter {

  public:
    static void init(uint32_t numThreads = 1)
    {
        if (s_isInitialized) {
            RP_CORE_WARN("AssetImporter already initialized");
            return;
        }
        s_assetImporters[AssetType::Shader] = loadShader;
        s_assetImporters[AssetType::Material] = loadMaterial;
        s_assetImporters[AssetType::Texture] = loadTexture;
        s_assetImporters[AssetType::Cubemap] = loadCubemap;
        s_isInitialized = true;

        if (s_threadRunning) {
            RP_CORE_WARN("AssetImporter already initialized");
            return;
        }

        s_threadRunning = true;

        unsigned int maxThreads = std::thread::hardware_concurrency();
        s_workerThreads.resize(std::min(numThreads, maxThreads));
        for (size_t i = 0; i < s_workerThreads.size(); ++i) {
            s_workerThreads[i] = std::thread(&AssetImporter::assetLoadThread);
        }
    }

    static void shutdown()
    {
        if (!s_isInitialized) {
            RP_CORE_WARN("AssetImporter not initialized");
            return;
        }

        // First, stop all worker threads
        shutdownWorkers();

        // Clear all queues to prevent any pending operations
        {
            std::lock_guard<std::mutex> lock(s_queueMutex);
            while (!s_pendingRequests.empty()) {
                s_pendingRequests.pop();
            }
        }

        s_assetImporters.clear();
        s_isInitialized = false;
    }

    static std::shared_ptr<Asset> importAsset(const AssetHandle &handle, const AssetMetadata &metadata)
    {

        return s_assetImporters[metadata.m_assetType](handle, metadata);
    }

  private:
    static std::shared_ptr<Asset> loadShader(const AssetHandle &handle, const AssetMetadata &metadata);

    static std::shared_ptr<Asset> loadMaterial(const AssetHandle &handle, const AssetMetadata &metadata);

    static std::shared_ptr<Asset> loadTexture(const AssetHandle &handle, const AssetMetadata &metadata);
    static std::shared_ptr<Asset> loadCubemap(const AssetHandle &handle, const AssetMetadata &metadata);

    static void assetLoadThread();

    static void shutdownWorkers();

  private:
    static bool s_isInitialized;
    static std::queue<LoadRequest> s_pendingRequests;

    // Thread management
    static std::vector<std::thread> s_workerThreads;
    static std::atomic<bool> s_threadRunning;
    static std::mutex s_queueMutex;
};

} // namespace Rapture

// for loading meshes, we can load each primitive as an asset if loaded trough a non-static mesh
// -> need a way to save multiple meshes belonging to the same gltf file, same for the materials
//
// what is a model asset?

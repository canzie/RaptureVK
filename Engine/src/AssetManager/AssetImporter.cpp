#include "AssetImporter.h"

#include "Logging/Log.h"

#include "AssetLoadRequests.h"
#include "Events/AssetEvents.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"
#include <filesystem>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace Rapture {

#define FILE_NOT_FOUND_ERROR(path) RP_CORE_ERROR("File not found: {}", path.string());

bool AssetImporter::s_isInitialized = false;
std::queue<LoadRequest> AssetImporter::s_pendingRequests;
std::vector<std::thread> AssetImporter::s_workerThreads;
std::atomic<bool> AssetImporter::s_threadRunning = false;
std::mutex AssetImporter::s_queueMutex;
std::condition_variable AssetImporter::s_queueCondition;

std::shared_ptr<Asset> AssetImporter::loadShader(const AssetHandle &handle, const AssetMetadata &metadata)
{

    const auto &initialPath = metadata.m_filePath;
    if (!std::filesystem::exists(initialPath)) {
        FILE_NOT_FOUND_ERROR(initialPath);
        return nullptr;
    }

    ShaderCompileInfo compileInfo = {};

    if (std::holds_alternative<ShaderImportConfig>(metadata.m_importConfig)) {
        auto shaderImportConfig = std::get<ShaderImportConfig>(metadata.m_importConfig);
        compileInfo = shaderImportConfig.compileInfo;
    }

    // Determine the type of the initial shader file
    std::string initialPathStr = initialPath.string();
    std::regex stageRegex("\\.(vert|vs|frag|fs|geom|gs|comp|cs)\\.[^.]+$");
    std::smatch stageMatch;
    std::string initialStageType;

    if (std::regex_search(initialPathStr, stageMatch, stageRegex) && stageMatch.size() > 1) {
        std::string stageExt = stageMatch[1].str();
        if (stageExt == "vert" || stageExt == "vs") initialStageType = "vertex";
        else if (stageExt == "frag" || stageExt == "fs") initialStageType = "fragment";
        else if (stageExt == "geom" || stageExt == "gs") initialStageType = "geometry";
        else if (stageExt == "comp" || stageExt == "cs") initialStageType = "compute";
    }

    if (initialStageType.empty()) {
        RP_CORE_ERROR("Could not determine shader "
                      "stage from file name: {}",
                      initialPath.string());
        return nullptr;
    }

    std::shared_ptr<Shader> shader;

    // Handle Compute Shaders (Standalone)
    if (initialStageType == "compute") {
        auto computePathOpt = getRelatedShaderPath(initialPath, "compute");

        if (!computePathOpt) {
            RP_CORE_ERROR("Could not find compute shader "
                          "related to: {}",
                          initialPath.string());
            return nullptr;
        }

        shader = std::make_shared<Shader>(*computePathOpt, compileInfo);

        if (!shader) {
            RP_CORE_ERROR("Failed to create or compile "
                          "shader from {}",
                          initialPath.string());
            return nullptr;
        }

    } else {
        // Find required vertex and fragment shaders
        auto vertexPathOpt = getRelatedShaderPath(initialPath, "vertex");
        auto fragmentPathOpt = getRelatedShaderPath(initialPath, "fragment");

        if (!vertexPathOpt) {
            RP_CORE_ERROR("Could not find vertex shader "
                          "related to: {}",
                          initialPath.string());
            return nullptr;
        }
        if (!fragmentPathOpt) {
            RP_CORE_INFO("No fragment shader found, "
                         "assuming vertex only shader for: {}",
                         initialPath.string());
        }

        // Optionally find geometry shader
        auto geometryPathOpt = getRelatedShaderPath(initialPath, "geometry");

        std::filesystem::path vertexPath = *vertexPathOpt;
        std::filesystem::path fragmentPath = fragmentPathOpt ? *fragmentPathOpt : std::filesystem::path();

        if (geometryPathOpt) {
            std::filesystem::path geometryPath = *geometryPathOpt;
            shader = std::make_shared<Shader>(vertexPath, fragmentPath, compileInfo);
        } else {
            shader = std::make_shared<Shader>(vertexPath, fragmentPath, compileInfo);
        }

        if (!shader) {
            RP_CORE_ERROR("Failed to create or compile shader from {} and {}{}", vertexPath.string(), fragmentPath.string(),
                          geometryPathOpt ? " and " + geometryPathOpt->string() : "");
            return nullptr;
        }
    }

    // Wrap the shader in an Asset object
    AssetVariant assetVariant = shader;
    std::shared_ptr<AssetVariant> variantPtr = std::make_shared<AssetVariant>(assetVariant);
    std::shared_ptr<Asset> asset = std::make_shared<Asset>(variantPtr);

    asset->m_status = AssetStatus::LOADED;
    asset->m_handle = handle;

    AssetEvents::onAssetLoaded().publish(handle);

    return asset;
}

std::shared_ptr<Asset> AssetImporter::loadMaterial(const AssetHandle &handle, const AssetMetadata &metadata)
{
    (void)handle;
    (void)metadata;

    RP_CORE_ERROR("Not implemented");
    return nullptr;
}

std::shared_ptr<Asset> AssetImporter::loadTexture(const AssetHandle &handle, const AssetMetadata &metadata)
{

    // Start worker thread if not already running
    if (!s_threadRunning) {
        RP_CORE_ERROR("Thread not running, failed to load texture '{0}'", metadata.m_filePath.string());
        return nullptr;
    }

    TextureSpecification texSpec = TextureSpecification();
    texSpec.mipLevels = 0; // Auto-calculate mip levels

    if (std::holds_alternative<TextureImportConfig>(metadata.m_importConfig)) {
        auto importConfig = std::get<TextureImportConfig>(metadata.m_importConfig);
        texSpec.srgb = importConfig.srgb;
    }

    auto tex = std::make_shared<Texture>(metadata.m_filePath.string(), texSpec, true);

    AssetVariant assetVariant = tex;
    std::shared_ptr<AssetVariant> variantPtr = std::make_shared<AssetVariant>(assetVariant);
    std::shared_ptr<Asset> asset = std::make_shared<Asset>(variantPtr);
    asset->m_status = AssetStatus::LOADING;
    asset->m_handle = handle;

    LoadRequest request;
    request.asset = asset;
    request.callback = [](std::shared_ptr<Asset> _asset) {
        _asset->getUnderlyingAsset<Texture>()->setReadyForSampling(true);
        _asset->m_status = AssetStatus::LOADED;
        AssetEvents::onAssetLoaded().publish(_asset->m_handle);
    };

    // Add the texture to the pending requests
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_pendingRequests.push(std::move(request));
    }
    s_queueCondition.notify_one();

    return asset;
}

std::shared_ptr<Asset> AssetImporter::loadCubemap(const AssetHandle &handle, const AssetMetadata &metadata)
{
    // Start worker thread if not already running
    if (!s_threadRunning) {
        RP_CORE_ERROR("Thread not running, failed to load texture '{0}'", metadata.m_filePath.string());
        return nullptr;
    }

    // read the .cubemap file
    std::vector<std::string> cubemapPaths = getCubemapPaths(metadata.m_filePath);
    if (cubemapPaths.size() != 6) {
        RP_CORE_ERROR("Cubemap file must contain exactly 6 paths. File: {}", metadata.m_filePath.string());
        return nullptr;
    }

    auto tex = std::make_shared<Texture>(cubemapPaths, TextureSpecification(), true);

    AssetVariant assetVariant = tex;
    std::shared_ptr<AssetVariant> variantPtr = std::make_shared<AssetVariant>(assetVariant);
    std::shared_ptr<Asset> asset = std::make_shared<Asset>(variantPtr);
    asset->m_status = AssetStatus::LOADING;
    asset->m_handle = handle;

    LoadRequest request;
    request.asset = asset;
    request.callback = [](std::shared_ptr<Asset> _asset) {
        _asset->getUnderlyingAsset<Texture>()->setReadyForSampling(true);
        _asset->m_status = AssetStatus::LOADED;
        AssetEvents::onAssetLoaded().publish(_asset->m_handle);
    };

    // Add the texture to the pending requests
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_pendingRequests.push(std::move(request));
    }
    s_queueCondition.notify_one();

    return asset;
}

void AssetImporter::assetLoadThread()
{
    RP_CORE_INFO("Asset loading thread started");

    while (true) {
        LoadRequest request;

        // Wait for work or shutdown signal
        {
            std::unique_lock<std::mutex> lock(s_queueMutex);
            s_queueCondition.wait(lock, [] {
                return !s_pendingRequests.empty() || !s_threadRunning;
            });

            // Check shutdown after waking
            if (!s_threadRunning && s_pendingRequests.empty()) {
                break;
            }

            // If shutting down but queue not empty, process remaining work
            if (s_pendingRequests.empty()) {
                continue;
            }

            request = std::move(s_pendingRequests.front());
            s_pendingRequests.pop();
        }

        // Process request outside of lock
        size_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());

        if (auto tex = request.asset->getUnderlyingAsset<Texture>()) {
            tex->loadImageFromFile(threadId);

            if (request.callback) {
                request.callback(request.asset);
            }
        }
    }

    RP_CORE_INFO("Asset loading thread exiting");
}

void AssetImporter::shutdownWorkers()
{
    RP_CORE_INFO("AssetImporter: Shutting down worker threads");

    // Signal threads to stop
    bool wasRunning = s_threadRunning.exchange(false);
    if (!wasRunning) {
        RP_CORE_INFO("AssetImporter: Worker threads already stopped");
        return;
    }

    // Wake all waiting threads so they can see the shutdown signal
    s_queueCondition.notify_all();

    // Join all threads
    RP_CORE_INFO("AssetImporter: Waiting for {} worker threads to join", s_workerThreads.size());
    for (auto &thread : s_workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    s_workerThreads.clear();
    RP_CORE_INFO("AssetImporter: Worker threads joined successfully");
}
} // namespace Rapture

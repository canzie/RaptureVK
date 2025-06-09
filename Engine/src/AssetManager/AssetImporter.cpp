#include "AssetImporter.h"

#include "Logging/Log.h"

#include "Shaders/Shader.h"
#include "Textures/Texture.h"
#include "AssetLoadRequests.h"
#include <filesystem>
#include <string>
#include <regex>
#include <optional>
#include <map>
#include <vector>
#include <variant>
#include "Events/AssetEvents.h"

namespace Rapture {

bool AssetImporter::s_isInitialized = false;
std::queue<LoadRequest> AssetImporter::s_pendingRequests;
std::vector<std::thread> AssetImporter::s_workerThreads;
std::atomic<bool> AssetImporter::s_threadRunning = false;
std::mutex AssetImporter::s_queueMutex;

// Helper function to find related shader file paths
std::optional<std::filesystem::path> getRelatedShaderPath(
    const std::filesystem::path& basePath,
    const std::string& targetStage) 
{
    if (!std::filesystem::exists(basePath)) {
        RP_CORE_WARN("AssetImporter::getRelatedShaderPath - Base path does not exist: {}", basePath.string());
        return std::nullopt;
    }

    auto directory = basePath.parent_path();
    
    // First try the simple naming convention (vert.spv -> frag.spv)
    if (targetStage == "fragment" && (basePath.filename().string().starts_with("vert"))) {
        // Try both frag.spv and frag.fs.spv
        std::vector<std::filesystem::path> potentialPaths = {
            directory / "frag.spv",
            directory / "frag.fs.spv"
        };
        
        for (const auto& path : potentialPaths) {
            if (std::filesystem::exists(path)) {
                return path;
            }
        }
    }
    
    // If simple naming didn't work, try the complex naming convention
    std::string basePathStr = basePath.string();
    // Regex to capture: (base_name)(.stage)(.extension)
    // Example: "path/to/MyShader.vert.glsl" -> ("path/to/MyShader")(".vert")(".glsl")
    std::regex pathRegex("^(.*?)(\\.(?:vert|vs|frag|fs|geom|gs|comp|cs))(\\.[^.]+)$");
    std::smatch match;

    if (!std::regex_match(basePathStr, match, pathRegex) || match.size() != 4) {
        RP_CORE_WARN("AssetImporter::getRelatedShaderPath - Could not parse base shader path structure: {}. Expected format like 'name.stage.ext'.", basePathStr);
        return std::nullopt;
    }

    std::string baseName = match[1].str();
    std::string finalExt = match[3].str();

    const std::map<std::string, std::array<std::string, 2>> stageExtensions = {
        {"vertex", {".vert", ".vs"}},
        {"fragment", {".frag", ".fs"}},
        {"geometry", {".geom", ".gs"}},
        {"compute", {".comp", ".cs"}}
    };

    if (stageExtensions.find(targetStage) == stageExtensions.end()) {
        RP_CORE_ERROR("AssetImporter::getRelatedShaderPath - Invalid target shader stage requested: {}", targetStage);
        return std::nullopt;
    }

    for (const auto& ext : stageExtensions.at(targetStage)) {
        std::filesystem::path potentialPath = baseName + ext + finalExt;
        if (std::filesystem::exists(potentialPath)) {
            return potentialPath;
        }
    }

    RP_CORE_WARN("AssetImporter::getRelatedShaderPath - Could not find related {} shader for base path: {}", targetStage, basePath.string());
    return std::nullopt;
}







    std::shared_ptr<Asset> AssetImporter::loadShader(const AssetHandle& handle, const AssetMetadata& metadata){

        const auto& initialPath = metadata.m_filePath;
        if (!std::filesystem::exists(initialPath)) {
            RP_CORE_ERROR("AssetImporter::loadShader - Initial shader file not found: {}", initialPath.string());
            return nullptr;
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
            RP_CORE_ERROR("AssetImporter::loadShader - Could not determine shader stage from file name: {}", initialPath.string());
            return nullptr;
        }

        std::shared_ptr<Shader> shader;

        // Handle Compute Shaders (Standalone)
        if (initialStageType == "compute") {
            auto computePathOpt = getRelatedShaderPath(initialPath, "compute");

            if (!computePathOpt) {
                RP_CORE_ERROR("AssetImporter::loadShader - Could not find compute shader related to: {}", initialPath.string());
                return nullptr;
            }

            shader = std::make_shared<Shader>(*computePathOpt);

            if (!shader) {
                RP_CORE_ERROR("AssetImporter::loadShader - Failed to create or compile shader from {}", initialPath.string());
                return nullptr;
            }

        } else {
            // Find required vertex and fragment shaders
            auto vertexPathOpt = getRelatedShaderPath(initialPath, "vertex");
            auto fragmentPathOpt = getRelatedShaderPath(initialPath, "fragment");

            if (!vertexPathOpt) {
                RP_CORE_ERROR("AssetImporter::loadShader - Could not find vertex shader related to: {}", initialPath.string());
                return nullptr;
            }
            if (!fragmentPathOpt) {
                RP_CORE_INFO("AssetImporter::loadShader - No fragment shader found, assuming vertex only shader for: {}", initialPath.string());
            }

            // Optionally find geometry shader
            auto geometryPathOpt = getRelatedShaderPath(initialPath, "geometry");

            std::filesystem::path vertexPath = *vertexPathOpt;
            std::filesystem::path fragmentPath = fragmentPathOpt ? *fragmentPathOpt : std::filesystem::path();

            if (geometryPathOpt) {
                std::filesystem::path geometryPath = *geometryPathOpt;
                shader = std::make_shared<Shader>(vertexPath, fragmentPath);
            } else {
                shader = std::make_shared<Shader>(vertexPath, fragmentPath);
            }

            if (!shader) {
                RP_CORE_ERROR("AssetImporter::loadShader - Failed to create or compile shader from {} and {}{}",
                    vertexPath.string(),
                    fragmentPath.string(),
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

    std::shared_ptr<Asset> AssetImporter::loadMaterial(const AssetHandle& handle, const AssetMetadata& metadata){        
        RP_CORE_ERROR("AssetImporter::loadMaterial - Not implemented");
        return nullptr;
    }

    std::shared_ptr<Asset> AssetImporter::loadTexture(const AssetHandle &handle, const AssetMetadata &metadata)
    {
        // Start worker thread if not already running
        if (!s_threadRunning) {
            RP_CORE_ERROR("TextureLibrary: Thread not running, failed to load texture '{0}'", metadata.m_filePath.string());
            return nullptr;
        }

        auto tex = std::make_shared<Texture>(metadata.m_filePath.string(), TextureFilter::Linear, TextureWrap::Repeat, true);
        //tex->setReadyForSampling(false);


        
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
            //RP_CORE_TRACE("AssetImporter::loadTexture - Texture loaded: {}", _asset->m_handle);
        };


        // Add the texture to the pending requests
        {
            std::lock_guard<std::mutex> lock(s_queueMutex);
            s_pendingRequests.push(request);
        }


        return asset;
    }

    void AssetImporter::assetLoadThread()
    {
        RP_CORE_INFO("AssetImporter: Asset loading thread started");
        
        while (s_threadRunning) {
            LoadRequest request;
            bool hasRequest = false;
            
            // Check if shutdown was requested before accessing the mutex
            if (!s_threadRunning) break;
            
            // Get a request from the queue
            {
                std::lock_guard<std::mutex> lock(s_queueMutex);
                if (!s_pendingRequests.empty()) {
                    request = s_pendingRequests.front();
                    s_pendingRequests.pop();
                    hasRequest = true;
                }
            }
            
            // Check if shutdown was requested after acquiring a request
            if (!s_threadRunning) break;
            
            if (hasRequest) {
                size_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());

                if (request.asset->getUnderlyingAsset<Texture>()) {
                    std::shared_ptr<Texture> tex = request.asset->getUnderlyingAsset<Texture>();
                    tex->loadImageFromFile(threadId);

                    if (request.callback) {
                        request.callback(request.asset);
                    }
                }
            } else {
                // No work to do, sleep to avoid busy waiting
                // Use shorter sleep durations to respond to shutdown quicker
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        
    }

void AssetImporter::shutdownWorkers()
{
    RP_CORE_INFO("AssetImporter: Shutting down worker threads");
    
    // Set the flag to false to signal threads to stop
    if (s_threadRunning.exchange(false)) {
        // Wait for the thread to join if joinable
        if (s_workerThreads.size() > 0 && s_workerThreads[0].joinable()) {
            RP_CORE_INFO("AssetImporter: Waiting for worker thread to join");
            for (auto& thread : s_workerThreads) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
            s_workerThreads.clear();
            RP_CORE_INFO("AssetImporter: Worker threads joined successfully");
        } else {
            RP_CORE_WARN("AssetImporter: Worker threads not joinable");
        }
    } else {
        RP_CORE_INFO("AssetImporter: Worker threads already stopped");
    }

}
}


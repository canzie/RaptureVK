#include "AssetImporter.h"

#include "AssetHelpers.h"
#include "Events/AssetEvents.h"
#include "Logging/Log.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"

#include <filesystem>
#include <regex>
#include <string>
#include <vector>

namespace Rapture {

#define FILE_NOT_FOUND_ERROR(path) RP_CORE_ERROR("File not found: {}", path.string());

bool AssetImporter::s_isInitialized = false;

bool AssetImporter::loadShader(Asset &asset, const AssetMetadata &metadata)
{
    const auto &initialPath = metadata.filePath;
    if (!std::filesystem::exists(initialPath)) {
        FILE_NOT_FOUND_ERROR(initialPath);
        asset.status = AssetStatus::FILE_NOT_FOUND;
        return false;
    }

    ShaderCompileInfo compileInfo = {};

    if (std::holds_alternative<ShaderImportConfig>(metadata.importConfig)) {
        auto shaderImportConfig = std::get<ShaderImportConfig>(metadata.importConfig);
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
        asset.status = AssetStatus::FAILED;
        return false;
    }

    std::unique_ptr<Shader> shader;

    // Handle Compute Shaders (Standalone)
    if (initialStageType == "compute") {
        auto computePathOpt = getRelatedShaderPath(initialPath, "compute");

        if (!computePathOpt) {
            RP_CORE_ERROR("Could not find compute shader "
                          "related to: {}",
                          initialPath.string());
            asset.status = AssetStatus::FAILED;
            return false;
        }

        shader = std::make_unique<Shader>(*computePathOpt, compileInfo);

        if (!shader) {
            RP_CORE_ERROR("Failed to create or compile "
                          "shader from {}",
                          initialPath.string());
            asset.status = AssetStatus::FAILED;
            return false;
        }

    } else {
        // Find required vertex and fragment shaders
        auto vertexPathOpt = getRelatedShaderPath(initialPath, "vertex");
        auto fragmentPathOpt = getRelatedShaderPath(initialPath, "fragment");

        if (!vertexPathOpt) {
            RP_CORE_ERROR("Could not find vertex shader "
                          "related to: {}",
                          initialPath.string());
            asset.status = AssetStatus::FAILED;
            return false;
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
            shader = std::make_unique<Shader>(vertexPath, fragmentPath, compileInfo);
        } else {
            shader = std::make_unique<Shader>(vertexPath, fragmentPath, compileInfo);
        }

        if (!shader) {
            RP_CORE_ERROR("Failed to create or compile shader from {} and {}{}", vertexPath.string(), fragmentPath.string(),
                          geometryPathOpt ? " and " + geometryPathOpt->string() : "");
            asset.status = AssetStatus::FAILED;
            return false;
        }
    }

    // Wrap the shader in an Asset object
    asset.status = shader->isReady() ? AssetStatus::LOADED : AssetStatus::FAILED;
    asset.setAssetVariant(std::move(shader));

    AssetEvents::onAssetLoaded().publish(asset.getHandle());

    return true;
}

bool AssetImporter::loadMaterial(Asset &asset, const AssetMetadata &metadata)
{
    (void)asset;
    (void)metadata;

    asset.status = AssetStatus::FAILED;

    RP_CORE_ERROR("Not implemented");
    return false;
}

bool AssetImporter::loadTexture(Asset &asset, const AssetMetadata &metadata)
{

    TextureSpecification texSpec = TextureSpecification();
    texSpec.mipLevels = 0; // 0 is auto
    if (std::holds_alternative<TextureImportConfig>(metadata.importConfig)) {
        auto importConfig = std::get<TextureImportConfig>(metadata.importConfig);
        texSpec.srgb = importConfig.srgb;
    }

    auto tex = std::make_unique<Texture>(metadata.filePath.string(), texSpec, false);
    tex->setReadyForSampling(true);

    asset.status = AssetStatus::LOADED; // TODO: need to be able to actually verify texture status
    asset.setAssetVariant(std::move(tex));

    AssetEvents::onAssetLoaded().publish(asset.getHandle());

    return true;
}

bool AssetImporter::loadCubemap(Asset &asset, const AssetMetadata &metadata)
{
    // read the .cubemap file
    std::vector<std::string> cubemapPaths = getCubemapPaths(metadata.filePath);
    if (cubemapPaths.size() != 6) {
        RP_CORE_ERROR("Cubemap file must contain exactly 6 paths. File: {}", metadata.filePath.string());
        asset.status = AssetStatus::FAILED;
        return false;
    }

    auto tex = std::make_unique<Texture>(cubemapPaths, TextureSpecification(), false);
    tex->setReadyForSampling(true);

    asset.status = AssetStatus::LOADED; // TODO: need to be able to actually verify texture status
    asset.setAssetVariant(std::move(tex));

    AssetEvents::onAssetLoaded().publish(asset.getHandle());
    return true;
}

} // namespace Rapture

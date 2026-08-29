#include "AssetImporter.h"

#include "AssetHelpers.h"
#include "core/events/AssetEvents.h"
#include "renderer/generators/textures/TextureCompressor.h"
#include "core/jobs/Counter.h"
#include "core/jobs/Job.h"
#include "core/jobs/JobSystem.h"
#include "assets/loaders/gltf/glTFCommon.h"
#include "core/utils/Log.h"
#include "assets/shaders/AShader.h"
#include "assets/textures/ATexture.h"
#include "gpu/shaders/Shader.h"
#include "gpu/textures/Texture.h"

#include <filesystem>
#include <memory>
#include <regex>
#include <span>
#include <string>
#include <vector>

namespace Rapture {

#define FILE_NOT_FOUND_ERROR(path) RP_CORE_ERROR("File not found: {}", path.string());

bool AssetImporter::s_isInitialized = false;

std::unique_ptr<Asset> AssetImporter::loadShader(AssetMetadata &metadata, AssetHandle handle)
{
    const auto &initialPath = metadata.getSourcePath();
    if (!std::filesystem::exists(initialPath)) {
        FILE_NOT_FOUND_ERROR(initialPath);
        return nullptr;
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
        return nullptr;
    }

    std::unique_ptr<Shader> shader;

    // Handle Compute Shaders (Standalone)
    if (initialStageType == "compute") {
        auto computePathOpt = getRelatedShaderPath(initialPath, "compute");

        if (!computePathOpt) {
            RP_CORE_ERROR("Could not find compute shader "
                          "related to: {}",
                          initialPath.string());
            return nullptr;
        }

        shader = std::make_unique<Shader>(*computePathOpt, compileInfo);

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
            shader = std::make_unique<Shader>(vertexPath, fragmentPath, compileInfo);
        } else {
            shader = std::make_unique<Shader>(vertexPath, fragmentPath, compileInfo);
        }

        if (!shader) {
            RP_CORE_ERROR("Failed to create or compile shader from {} and {}{}", vertexPath.string(), fragmentPath.string(),
                          geometryPathOpt ? " and " + geometryPathOpt->string() : "");
            return nullptr;
        }
    }

    auto asset = std::make_unique<AShader>(std::move(shader));
    asset->setHandle(handle);
    asset->setStatus(asset->shader().isReady() ? AssetStatus::LOADED : AssetStatus::FAILED);

    AssetEvents::onAssetLoaded().publish(handle);

    return asset;
}

std::unique_ptr<Asset> AssetImporter::loadMaterial(AssetMetadata &metadata, AssetHandle handle)
{
    (void)metadata;
    (void)handle;

    RP_CORE_ERROR("Not implemented");
    return nullptr;
}

std::unique_ptr<Asset> AssetImporter::loadTexture(AssetMetadata &metadata, AssetHandle handle)
{
    TextureSpecification texSpec = TextureSpecification();
    texSpec.mipLevels = 0; // 0 is auto
    texSpec.type = TextureType::TEXTURE2D;
    texSpec.format = TextureFormat::RGBA8; // source decoder always produces 4-channel RGBA8 data

    if (std::holds_alternative<TextureImportConfig>(metadata.importConfig)) {
        auto importConfig = std::get<TextureImportConfig>(metadata.importConfig);
        texSpec.format = importConfig.format;
        texSpec.srgb = importConfig.srgb;
    }

    // Serializing the compressed image reads it back, which needs transfer-source usage
    texSpec.allowReadback = isCompressedFormat(texSpec.format);

    if (!getImageDimensions(metadata.getSourcePath(), texSpec.width, texSpec.height)) {
        RP_CORE_ERROR("Failed to read texture dimensions: {}", metadata.getSourcePath().string());
        return nullptr;
    }

    auto tex = Texture::createPlaceholder(texSpec);
    Texture *texPtr = tex.get();
    std::filesystem::path path = metadata.getSourcePath();

    auto asset = std::make_unique<ATexture>(std::move(tex));
    asset->setHandle(handle);
    asset->setStatus(AssetStatus::LOADING);
    Asset *assetPtr = asset.get();

    jobs().run(JobDeclaration(
        [texPtr, assetPtr, path](JobContext &jctx) {
            Counter ioCounter{};
            ioCounter.increment();

            auto ioData = std::make_shared<std::pair<std::vector<uint8_t>, bool>>();

            jobs().requestIo(
                path,
                [ioData, &ioCounter](std::vector<uint8_t> &&data, bool success) {
                    ioData->first = std::move(data);
                    ioData->second = success;
                    ioCounter.decrement();
                },
                JobPriority::LOW);

            jctx.waitFor(ioCounter, 0);

            if (!ioData->second) {
                RP_CORE_ERROR("Failed to load texture file: {}", path.string());
                texPtr->markFailed();
                assetPtr->setStatus(AssetStatus::FAILED);
                return;
            }

            DecodedImageData decoded = decodeImageMemory(ioData->first);
            if (!decoded.success) {
                RP_CORE_ERROR("Failed to decode texture: {}", path.string());
                texPtr->markFailed();
                assetPtr->setStatus(AssetStatus::FAILED);
                return;
            }

            TextureFormat targetFormat = texPtr->getSpecification().format;
            if (isCompressedFormat(targetFormat)) {
                TextureCompressor compressor(std::move(decoded.pixels), decoded.width, decoded.height);
                bool compressed = false;
                if (compressor.isValid()) {
                    switch (targetFormat) {
                    case TextureFormat::BC1_RGB:
                    case TextureFormat::BC1_RGBA:
                        compressed = compressor.compressToBC1(jctx, *texPtr);
                        break;
                    case TextureFormat::BC3:
                        compressed = compressor.compressToBC3(jctx, *texPtr);
                        break;
                    case TextureFormat::BC4:
                        compressed = compressor.compressToBC4(jctx, *texPtr);
                        break;
                    case TextureFormat::BC5:
                        compressed = compressor.compressToBC5(jctx, *texPtr);
                        break;
                    default:
                        break;
                    }
                }

                if (!compressed) {
                    RP_CORE_ERROR("Failed to compress texture: {}", path.string());
                    texPtr->markFailed();
                    assetPtr->setStatus(AssetStatus::FAILED);
                    return;
                }
            } else {
                texPtr->uploadDataAsync(std::move(decoded.pixels));
            }

            assetPtr->setStatus(AssetStatus::LOADED);
            AssetEvents::onAssetLoaded().publish(assetPtr->handle());
        },
        JobPriority::NORMAL, QueueAffinity::ANY, nullptr, "Texture decode"));

    return asset;
}

std::unique_ptr<Asset> AssetImporter::loadCubemap(AssetMetadata &metadata, AssetHandle handle)
{
    std::vector<std::string> cubemapPaths = getCubemapPaths(metadata.getSourcePath());
    if (cubemapPaths.size() != 6) {
        RP_CORE_ERROR("Cubemap file must contain exactly 6 paths. File: {}", metadata.getSourcePath().string());
        return nullptr;
    }

    std::vector<DecodedImageData> decodedFaces;
    decodedFaces.reserve(cubemapPaths.size());

    uint32_t width = 0, height = 0;
    for (size_t i = 0; i < cubemapPaths.size(); ++i) {
        DecodedImageData decoded = decodeImageFile(cubemapPaths[i]);
        if (!decoded.success) {
            RP_CORE_ERROR("Failed to decode cubemap face: {}", cubemapPaths[i]);
            return nullptr;
        }
        if (i == 0) {
            width = decoded.width;
            height = decoded.height;
        }
        decodedFaces.push_back(std::move(decoded));
    }

    TextureSpecification texSpec{};
    texSpec.type = TextureType::TEXTURECUBE;
    texSpec.width = width;
    texSpec.height = height;
    texSpec.format = TextureFormat::RGBA8;
    texSpec.mipLevels = 0;

    std::vector<std::span<const uint8_t>> layerData;
    layerData.reserve(decodedFaces.size());
    for (const auto &face : decodedFaces) {
        layerData.emplace_back(face.pixels);
    }

    auto tex = std::make_unique<Texture>(texSpec, layerData);

    auto asset = std::make_unique<ATexture>(std::move(tex));
    asset->setHandle(handle);
    asset->setStatus(AssetStatus::LOADED);

    AssetEvents::onAssetLoaded().publish(handle);
    return asset;
}

} // namespace Rapture

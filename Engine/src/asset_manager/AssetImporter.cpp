#include "AssetImporter.h"

#include "AssetHelpers.h"
#include "events/AssetEvents.h"
#include "generators/textures/TextureCompressor.h"
#include "jobs/Counter.h"
#include "jobs/Job.h"
#include "jobs/JobSystem.h"
#include "loaders/gltf/glTFCommon.h"
#include "logging/Log.h"
#include "shaders/Shader.h"
#include "textures/Texture.h"

#include <filesystem>
#include <memory>
#include <regex>
#include <span>
#include <string>
#include <vector>

namespace Rapture {

#define FILE_NOT_FOUND_ERROR(path) RP_CORE_ERROR("File not found: {}", path.string());

bool AssetImporter::s_isInitialized = false;

bool AssetImporter::loadShader(Asset &asset, AssetMetadata &metadata)
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

bool AssetImporter::loadMaterial(Asset &asset, AssetMetadata &metadata)
{
    (void)asset;
    (void)metadata;

    asset.status = AssetStatus::FAILED;

    RP_CORE_ERROR("Not implemented");
    return false;
}

bool AssetImporter::loadTexture(Asset &asset, AssetMetadata &metadata)
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

    if (!getImageDimensions(metadata.filePath, texSpec.width, texSpec.height)) {
        RP_CORE_ERROR("Failed to read texture dimensions: {}", metadata.filePath.string());
        asset.status = AssetStatus::FAILED;
        return false;
    }

    auto tex = Texture::createPlaceholder(texSpec);
    Texture *texPtr = tex.get();
    Asset *assetPtr = &asset;
    std::filesystem::path path = metadata.filePath;

    asset.status = AssetStatus::LOADING;
    asset.setAssetVariant(std::move(tex));

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
                assetPtr->status = AssetStatus::FAILED;
                return;
            }

            DecodedImageData decoded = decodeImageMemory(ioData->first);
            if (!decoded.success) {
                RP_CORE_ERROR("Failed to decode texture: {}", path.string());
                texPtr->markFailed();
                assetPtr->status = AssetStatus::FAILED;
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
                    assetPtr->status = AssetStatus::FAILED;
                    return;
                }
            } else {
                texPtr->uploadDataAsync(std::move(decoded.pixels));
            }

            assetPtr->status = AssetStatus::LOADED;
            AssetEvents::onAssetLoaded().publish(assetPtr->getHandle());
        },
        JobPriority::NORMAL, QueueAffinity::ANY, nullptr, "Texture decode"));

    return true;
}

bool AssetImporter::loadCubemap(Asset &asset, AssetMetadata &metadata)
{
    std::vector<std::string> cubemapPaths = getCubemapPaths(metadata.filePath);
    if (cubemapPaths.size() != 6) {
        RP_CORE_ERROR("Cubemap file must contain exactly 6 paths. File: {}", metadata.filePath.string());
        asset.status = AssetStatus::FAILED;
        return false;
    }

    std::vector<DecodedImageData> decodedFaces;
    decodedFaces.reserve(cubemapPaths.size());

    uint32_t width = 0, height = 0;
    for (size_t i = 0; i < cubemapPaths.size(); ++i) {
        DecodedImageData decoded = decodeImageFile(cubemapPaths[i]);
        if (!decoded.success) {
            RP_CORE_ERROR("Failed to decode cubemap face: {}", cubemapPaths[i]);
            asset.status = AssetStatus::FAILED;
            return false;
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

    asset.status = AssetStatus::LOADED;
    asset.setAssetVariant(std::move(tex));

    AssetEvents::onAssetLoaded().publish(asset.getHandle());
    return true;
}

bool AssetImporter::loadScene(Asset &asset, AssetMetadata &metadata)
{
    const auto &path = metadata.filePath;
    if (!std::filesystem::exists(path)) {
        FILE_NOT_FOUND_ERROR(path);
        asset.status = AssetStatus::FILE_NOT_FOUND;
        return false;
    }

    std::string extension = path.extension().string();
    for (char &c : extension) {
        c = std::tolower(c);
    }

    auto sceneData = std::make_unique<SceneFileData>();
    sceneData->metadata.sourcePath = path;

    if (extension == ".gltf" || extension == ".glb") {
        RP_CORE_WARN("glTF loading not yet implemented via asset importer");
        asset.status = AssetStatus::FAILED;
        return false;
    } else if (extension == ".fbx") {
        RP_CORE_WARN("FBX loading not supported");
        asset.status = AssetStatus::FAILED;
        return false;
    }

    asset.status = AssetStatus::FAILED;
    return false;
}

} // namespace Rapture

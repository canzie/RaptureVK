#ifndef RAPTURE__ASSET_IMPORT_CONFIG_H
#define RAPTURE__ASSET_IMPORT_CONFIG_H

#include "AssetCommon.h"
#include "components/systems/Prefab.h"
#include "meshes/Mesh.h"
#include "shaders/Shader.h"
#include "textures/TextureCommon.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace Rapture {

struct ShaderImportConfig {

    ShaderCompileInfo compileInfo;

    bool operator==(const ShaderImportConfig &other) const
    {
        return compileInfo.macros == other.compileInfo.macros && compileInfo.includePath == other.compileInfo.includePath;
    }
};

struct TextureImportConfig {
    TextureFormat format = TextureFormat::RGBA8; // Format to decode/compress source data into
    bool srgb = false;

    bool operator==(const TextureImportConfig &other) const { return format == other.format && srgb == other.srgb; }
};

using AssetImportConfigVariant = std::variant<std::monostate, ShaderImportConfig, TextureImportConfig>;

struct MeshImportData {
    MeshAllocatorParams params;
};

struct PrefabImportData {
    std::unique_ptr<Prefab> prefab;
};

class BaseMaterial;
class MaterialInstance;

struct BaseMaterialImportData {
    std::unique_ptr<BaseMaterial> material;
};

struct MaterialInstanceImportData {
    std::unique_ptr<MaterialInstance> instance;
};

using AssetImportDataVariant =
    std::variant<std::monostate, MeshImportData, PrefabImportData, BaseMaterialImportData, MaterialInstanceImportData>;

struct AssetImportFileRequest {
    std::filesystem::path source;
    std::filesystem::path output = {};
    AssetImportConfigVariant config = std::monostate();
    std::string name = {};
};

struct AssetImportDataRequest {
    AssetImportDataVariant data;
    std::filesystem::path output = {};
    std::string name = {};
    std::optional<AssetProvenance> provenance = std::nullopt;
};

} // namespace Rapture

#endif // RAPTURE__ASSET_IMPORT_CONFIG_H

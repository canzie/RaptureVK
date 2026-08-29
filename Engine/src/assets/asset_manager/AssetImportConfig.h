#ifndef RAPTURE__ASSET_IMPORT_CONFIG_H
#define RAPTURE__ASSET_IMPORT_CONFIG_H

#include "AssetCommon.h"
#include "gpu/shaders/Shader.h"
#include "gpu/textures/TextureCommon.h"

#include <filesystem>
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

struct AssetImportFileRequest {
    std::filesystem::path source;
    std::filesystem::path output = {};
    AssetImportConfigVariant config = std::monostate();
    std::string name = {};
};

} // namespace Rapture

#endif // RAPTURE__ASSET_IMPORT_CONFIG_H

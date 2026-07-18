#pragma once

#include "meshes/Mesh.h"
#include "shaders/Shader.h"
#include "textures/TextureCommon.h"

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
    bool writeBlob = true;
};

using AssetImportDataVariant = std::variant<std::monostate, MeshImportData>;

} // namespace Rapture

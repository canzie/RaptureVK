#pragma once

#include "Shaders/Shader.h"

#include <vector>
#include <string>
#include <variant>

namespace Rapture {
    
    struct ShaderImportConfig {

        ShaderCompileInfo compileInfo;

        bool operator==(const ShaderImportConfig& other) const {
            return compileInfo.macros == other.compileInfo.macros;
        }

    };

    struct TextureImportConfig {
        bool srgb = false;


        bool operator==(const TextureImportConfig& other) const {
            return srgb == other.srgb;
        }

    };



    using AssetImportConfigVariant = std::variant<std::monostate, ShaderImportConfig, TextureImportConfig>;

}
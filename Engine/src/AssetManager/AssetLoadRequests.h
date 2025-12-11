#pragma once

#include <functional>
#include <memory>
#include <string>
#include <variant>

#include "Asset.h"

#include "Textures/Texture.h"

namespace Rapture {

struct LoadRequest {
    std::string path;
    std::shared_ptr<Asset> asset;
    std::function<void(std::shared_ptr<Asset>)> callback = nullptr;
};

} // namespace Rapture

// jobs,
#pragma once

#include <string>
#include <memory>
#include <functional>
#include <variant>


#include "Asset.h"

#include "Textures/Texture.h"

namespace Rapture {

struct LoadRequest {
    std::string path;
    std::shared_ptr<Asset> asset;
    std::function<void(std::shared_ptr<Asset>)> callback = nullptr;  
};




}

// jobs, 
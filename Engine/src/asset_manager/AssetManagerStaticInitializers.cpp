#include "AssetManager.h"

#include "generators/textures/TextureCompressor.h"
#include "logging/Log.h"
#include "utils/TextureFlattener.h"

namespace Rapture {

bool AssetManager::s_isInitialized = false;

AssetManagerEditor *AssetManager::s_activeAssetManager = nullptr;

void AssetManager::shutdown()
{
    if (!s_isInitialized) {
        RP_CORE_WARN("AssetManager not initialized");
        return;
    }
    TextureFlattener::shutdown();
    TextureCompressor::shutdown();
    delete s_activeAssetManager;
    s_isInitialized = false;
}

} // namespace Rapture

#include "AssetManager.h"

#include "renderer/generators/textures/TextureCompressor.h"
#include "core/utils/Log.h"
#include "core/utils/TextureFlattener.h"

namespace Rapture {

bool AssetManager::s_isInitialized = false;

AssetManagerEditor *AssetManager::s_activeAssetManager = nullptr;

EventListenerId AssetManager::s_serializeListener = 0;
EventListenerId AssetManager::s_registerListener = 0;

void AssetManager::shutdown()
{
    if (!s_isInitialized) {
        RP_CORE_WARN("AssetManager not initialized");
        return;
    }
    ProjectEvents::onProjectSerialize().removeListener(s_serializeListener);
    ProjectEvents::onProjectRegister().removeListener(s_registerListener);
    TextureFlattener::shutdown();
    TextureCompressor::shutdown();
    delete s_activeAssetManager;
    s_isInitialized = false;
}

} // namespace Rapture

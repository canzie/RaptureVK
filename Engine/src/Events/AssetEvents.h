#pragma once

#include "Events.h" 
#include "AssetManager/Asset.h"

namespace Rapture {
namespace AssetEvents {

    // Input: Keyboard Events
    using AssetLoadedEvent = EventBus<AssetHandle /*handle*/>;


    // Accessors for Input: Keyboard Events
    inline AssetLoadedEvent& onAssetLoaded() {
        return EventRegistry::getInstance().getEventBus<AssetHandle /*handle*/>("AssetLoaded");
    }


} // namespace AssetEvents
} // namespace Rapture 
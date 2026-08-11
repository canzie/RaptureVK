#pragma once

#include "Events.h"
#include "asset_manager/AssetCommon.h"
#include "scenes/entities/EntityCommon.h"

namespace Rapture {
class MaterialInstance;

namespace AssetEvents {

// Input: Keyboard Events
using AssetLoadedEvent = EventBus<AssetHandle /*handle*/>;
using MaterialChangedEvent = EventBus<AssetHandle /*handle*/>;

using MaterialInstanceChangedEvent = EventBus<MaterialInstance * /*material*/>;

// Accessors for Input: Keyboard Events
inline AssetLoadedEvent &onAssetLoaded()
{
    return EventRegistry::getInstance().getEventBus<AssetHandle /*handle*/>("AssetLoaded");
}

// Accessors for Input: Keyboard Events
inline MaterialChangedEvent &onMaterialChanged()
{
    return EventRegistry::getInstance().getEventBus<AssetHandle /*handle*/>("MaterialChanged");
}

// Accessor for MaterialInstanceChangedEvent
inline MaterialInstanceChangedEvent &onMaterialInstanceChanged()
{
    return EventRegistry::getInstance().getEventBus<MaterialInstance *>("MaterialInstanceChanged");
}

} // namespace AssetEvents
} // namespace Rapture

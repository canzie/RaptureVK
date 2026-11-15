#pragma once

#include <rapture/assets/asset_manager/AssetCommon.h>
#include <rapture/core/events/Events.h>
#include <rapture/scene/EntityCommon.h>

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

#pragma once

#include "AssetManager/Asset.h"
#include "Events.h"
#include "Scenes/Entities/EntityCommon.h"

namespace Rapture {
class MaterialInstance;

namespace AssetEvents {

// Input: Keyboard Events
using AssetLoadedEvent = EventBus<AssetHandle /*handle*/>;
using MaterialChangedEvent = EventBus<AssetHandle /*handle*/>;
using MeshTransformChangedEvent = EventBus<EntityID /*entity*/>;

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

// Gets called when a transform from a mesh has changed
// we publish this event in the scenes onupdate
inline MeshTransformChangedEvent &onMeshTransformChanged()
{
    return EventRegistry::getInstance().getEventBus<EntityID /*entity*/>("MeshTransformChanged");
}

} // namespace AssetEvents
} // namespace Rapture
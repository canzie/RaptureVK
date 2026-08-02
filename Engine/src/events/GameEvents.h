#pragma once
#include "Events.h"

namespace Rapture {
namespace GameEvents {
// Scene events
using SceneLoadRequestedEvent = EventBus<std::string>;
using SceneActivatedEvent = EventBus<Scene &>;
using SceneDeactivatedEvent = EventBus<Scene &>;

// World events
using WorldTransitionRequestedEvent = EventBus<std::string>;
using WorldActivatedEvent = EventBus<World *>;

// Layer communication events
using LayerCommunicationEvent = EventBus<std::string, std::string>;

// Project events
using ProjectLoadRequestedEvent = EventBus<std::string>;
using ProjectLoadedEvent = EventBus<std::string>;

using EntitySelectedEvent = EventBus<Entity>;
using EntityDeselectedEvent = EventBus<Entity>;

// Global event accessors
inline SceneLoadRequestedEvent &onSceneLoadRequested()
{
    return EventRegistry::getInstance().getEventBus<std::string>("SceneLoadRequested");
}

inline SceneActivatedEvent &onSceneActivated()
{
    return EventRegistry::getInstance().getEventBus<Scene &>("SceneActivated");
}

inline SceneDeactivatedEvent &onSceneDeactivated()
{
    return EventRegistry::getInstance().getEventBus<Scene &>("SceneDeactivated");
}

inline WorldTransitionRequestedEvent &onWorldTransitionRequested()
{
    return EventRegistry::getInstance().getEventBus<std::string>("WorldTransitionRequested");
}

inline WorldActivatedEvent &onWorldActivated()
{
    return EventRegistry::getInstance().getEventBus<World *>("WorldActivated");
}

inline LayerCommunicationEvent &onLayerCommunication()
{
    return EventRegistry::getInstance().getEventBus<std::string, std::string>("LayerCommunication");
}

inline ProjectLoadRequestedEvent &onProjectLoadRequested()
{
    return EventRegistry::getInstance().getEventBus<std::string>("ProjectLoadRequested");
}

inline ProjectLoadedEvent &onProjectLoaded()
{
    return EventRegistry::getInstance().getEventBus<std::string>("ProjectLoaded");
}

inline EntitySelectedEvent &onEntitySelected()
{
    return EventRegistry::getInstance().getEventBus<Entity>("EntitySelected");
}

inline EntityDeselectedEvent &onEntityDeselected()
{
    return EventRegistry::getInstance().getEventBus<Entity>("EntityDeselected");
}
} // namespace GameEvents
} // namespace Rapture

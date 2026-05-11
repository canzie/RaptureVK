#pragma once
#include "Events.h"

namespace Rapture {
namespace GameEvents {
// Scene events
using SceneLoadRequestedEvent = EventBus<std::string>;
using SceneActivatedEvent = EventBus<std::shared_ptr<Scene>>;
using SceneDeactivatedEvent = EventBus<std::shared_ptr<Scene>>;

// World events
using WorldTransitionRequestedEvent = EventBus<std::string>;
using WorldActivatedEvent = EventBus<std::shared_ptr<World>>;

// Layer communication events
using LayerCommunicationEvent = EventBus<std::string, std::string>;

// Project events
using ProjectLoadRequestedEvent = EventBus<std::string>;
using ProjectLoadedEvent = EventBus<std::string>;

using EntitySelectedEvent = EventBus<std::shared_ptr<Entity>>;

// Global event accessors
inline SceneLoadRequestedEvent &onSceneLoadRequested()
{
    return EventRegistry::getInstance().getEventBus<std::string>("SceneLoadRequested");
}

inline SceneActivatedEvent &onSceneActivated()
{
    return EventRegistry::getInstance().getEventBus<std::shared_ptr<Scene>>("SceneActivated");
}

inline SceneDeactivatedEvent &onSceneDeactivated()
{
    return EventRegistry::getInstance().getEventBus<std::shared_ptr<Scene>>("SceneDeactivated");
}

inline WorldTransitionRequestedEvent &onWorldTransitionRequested()
{
    return EventRegistry::getInstance().getEventBus<std::string>("WorldTransitionRequested");
}

inline WorldActivatedEvent &onWorldActivated()
{
    return EventRegistry::getInstance().getEventBus<std::shared_ptr<World>>("WorldActivated");
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
    return EventRegistry::getInstance().getEventBus<std::shared_ptr<Entity>>("EntitySelected");
}
} // namespace GameEvents
} // namespace Rapture
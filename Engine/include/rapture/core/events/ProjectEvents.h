#ifndef RAPTURE__PROJECT_EVENTS_H
#define RAPTURE__PROJECT_EVENTS_H

#include <rapture/core/events/Events.h>
#include <rapture/core/serialization/SerialDocument.h>

namespace Rapture {
namespace ProjectEvents {

using ProjectSerializeEvent = EventBus<WriteNode & /*root*/>;
using ProjectRegisterEvent = EventBus<ReadNode & /*root*/>;
using ProjectRegisterCompleteEvent = EventBus<>;

inline ProjectSerializeEvent &onProjectSerialize()
{
    return EventRegistry::getInstance().getEventBus<WriteNode &>("ProjectSerialize");
}

inline ProjectRegisterEvent &onProjectRegister()
{
    return EventRegistry::getInstance().getEventBus<ReadNode &>("ProjectRegister");
}

inline ProjectRegisterCompleteEvent &onProjectRegisterComplete()
{
    return EventRegistry::getInstance().getEventBus<>("ProjectRegisterComplete");
}

} // namespace ProjectEvents
} // namespace Rapture

#endif // RAPTURE__PROJECT_EVENTS_H

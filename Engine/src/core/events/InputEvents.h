#pragma once

#include "Events.h"

namespace Rapture {
namespace InputEvents {

// Input: Keyboard Events
using KeyPressedEvent = EventBus<int /*keyCode*/, int /*repeatCount*/>;
using KeyReleasedEvent = EventBus<int /*keyCode*/>;
using KeyTypedEvent = EventBus<unsigned int /*characterCode*/>; // Or char, depending on needs

// Input: Mouse Events
using MouseButtonPressedEvent = EventBus<int /*button*/>;
using MouseButtonReleasedEvent = EventBus<int /*button*/>;
using MouseMovedEvent = EventBus<float /*xPos*/, float /*yPos*/>;

// Accessors for Input: Keyboard Events
inline KeyPressedEvent &onKeyPressed()
{
    return EventRegistry::getInstance().getEventBus<int, int>("KeyPressed");
}
inline KeyReleasedEvent &onKeyReleased()
{
    return EventRegistry::getInstance().getEventBus<int>("KeyReleased");
}
inline KeyTypedEvent &onKeyTyped()
{
    return EventRegistry::getInstance().getEventBus<unsigned int>("KeyTyped");
}

// Accessors for Input: Mouse Events
inline MouseButtonPressedEvent &onMouseButtonPressed()
{
    return EventRegistry::getInstance().getEventBus<int>("MouseButtonPressed");
}
inline MouseButtonReleasedEvent &onMouseButtonReleased()
{
    return EventRegistry::getInstance().getEventBus<int>("MouseButtonReleased");
}
inline MouseMovedEvent &onMouseMoved()
{
    return EventRegistry::getInstance().getEventBus<float, float>("MouseMoved");
}

} // namespace InputEvents
} // namespace Rapture

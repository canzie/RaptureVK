#pragma once
// Entire content related to old Event system removed.
// This file can be deleted if no new application-specific EventBus definitions are added here later.
#include "Events.h" // For EventBus and EventRegistry

namespace Rapture {
namespace ApplicationEvents {

    // Application Window Events
    using WindowCloseEvent = EventBus<>;
    using WindowResizeEvent = EventBus<unsigned int /*width*/, unsigned int /*height*/>;
    using WindowFocusEvent = EventBus<>;
    using WindowLostFocusEvent = EventBus<>;
    using WindowMovedEvent = EventBus<unsigned int /*xPos*/, unsigned int /*yPos*/>;
    using SwapChainRecreatedEvent = EventBus<std::shared_ptr<SwapChain>>;
    using RequestSwapChainRecreationEvent = EventBus<>;
    // Application Lifecycle Events


    // Accessors for Application Window Events
    inline WindowCloseEvent& onWindowClose() {
        return EventRegistry::getInstance().getEventBus<>("WindowClose");
    }
    inline WindowResizeEvent& onWindowResize() {
        return EventRegistry::getInstance().getEventBus<unsigned int, unsigned int>("WindowResize");
    }
    inline WindowFocusEvent& onWindowFocus() {
        return EventRegistry::getInstance().getEventBus<>("WindowFocus");
    }
    inline WindowLostFocusEvent& onWindowLostFocus() {
        return EventRegistry::getInstance().getEventBus<>("WindowLostFocus");
    }
    inline WindowMovedEvent& onWindowMoved() {
        return EventRegistry::getInstance().getEventBus<unsigned int, unsigned int>("WindowMoved");
    }
    inline SwapChainRecreatedEvent& onSwapChainRecreated() {
        return EventRegistry::getInstance().getEventBus<std::shared_ptr<SwapChain>>("SwapChainRecreated");
    }

    inline RequestSwapChainRecreationEvent& onRequestSwapChainRecreation() {
        return EventRegistry::getInstance().getEventBus<>("RequestSwapChainRecreation");
    }

} // namespace ApplicationEvents
} // namespace Rapture
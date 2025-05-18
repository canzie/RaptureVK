#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <functional>

namespace Rapture {

    class Scene;
    class World;
    class Entity;
    
/**
 * EventBus - A templated event dispatcher for the observer pattern
 * Allows subscribing to and publishing events with arbitrary parameters
 */
template<typename... Args>
class EventBus {
public:
	using EventCallback = std::function<void(Args...)>;
	using ListenerID = size_t;
	
	// Add a listener to this event
	ListenerID addListener(const EventCallback& callback) {
		m_listeners[m_nextListenerID] = callback;
		return m_nextListenerID++;
	}
	
	// Remove a listener by ID
	void removeListener(ListenerID id) {
		m_listeners.erase(id);
	}
	
	// Publish an event to all listeners
	void publish(Args... args) const {
		for (const auto& [id, listener] : m_listeners) {
			listener(args...);
		}
	}
	
	// Alias for publish to maintain compatibility with existing code
	void invoke(Args... args) const {
		publish(args...);
	}

	// Clear all listeners from this event bus
	void clearListeners() {
		m_listeners.clear();
	}
	
private:
	std::unordered_map<ListenerID, EventCallback> m_listeners;
	ListenerID m_nextListenerID = 0;
};

// Type-erased event handler base class for the event registry
class BaseEventHandler {
public:
	virtual ~BaseEventHandler() = default;
	virtual void clearAllListeners() = 0; // Pure virtual function
};

// Concrete event handler for specific event types
template<typename... Args>
class EventHandler : public BaseEventHandler {
public:
	EventHandler() = default;
	
	EventBus<Args...>& getEventBus() { return m_eventBus; }

	void clearAllListeners() override {
		m_eventBus.clearListeners();
	}
	
private:
	EventBus<Args...> m_eventBus;
};


/**
 * EventRegistry - Global registry for event buses
 * Allows accessing event buses by name rather than by type
 */
class EventRegistry {
public:
	static EventRegistry& getInstance() {
		static EventRegistry instance;
		return instance;
	}
	
	// Get or create an event bus for the given name and types
	template<typename... Args>
	EventBus<Args...>& getEventBus(const std::string& name) {
		//std::type_index typeIdx = std::type_index(typeid(EventBus<Args...>));
		std::string typeId = typeid(EventBus<Args...>).name();

		// Create the type-event pair if it doesn't exist

        // Check if bus exists with correct type
        auto busIt = m_eventBuses.find(name);
        if (busIt == m_eventBuses.end() || 
            m_eventTypeNames[name] != typeId) {
            
            // Create new bus
            m_eventBuses[name] = std::make_shared<EventHandler<Args...>>();
            m_eventTypeNames[name] = typeId;
        }
		
		// Return the event bus
		return static_cast<EventHandler<Args...>*>(m_eventBuses[name].get())->getEventBus();
	}

	// Shutdown the event system and clear all listeners
	void shutdown() {
		for (auto& [name, eventHandler] : m_eventBuses) {
			if (eventHandler) {
				eventHandler->clearAllListeners();
			}
		}
		m_eventBuses.clear();
		m_eventTypeNames.clear();
	}
	
private:
	EventRegistry() = default;
	
	std::unordered_map<std::string, std::shared_ptr<BaseEventHandler>> m_eventBuses;
	std::unordered_map<std::string, std::string> m_eventTypeNames;
};



}

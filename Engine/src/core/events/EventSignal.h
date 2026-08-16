#ifndef RAPTURE__EVENT_SIGNAL_H
#define RAPTURE__EVENT_SIGNAL_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>

namespace Rapture {

template <typename> class EventSignal;

class EventSignalBase {
  public:
    EventSignalBase() : m_alive(std::make_shared<bool>(true)) {}

    virtual ~EventSignalBase() { *m_alive = false; }

    /**
     * @brief Removes a connection by its ID.
     * @param id The connection ID to remove.
     */
    virtual void removeConnection(uint32_t id) = 0;

  protected:
    bool m_firing = false;
    std::shared_ptr<bool> m_alive;
};

class EventConnection {
    template <typename> friend class EventSignal;

  public:
    EventConnection() = default;

    /**
     * @brief Destructor automatically disconnects from the
     * signal if still connected.
     */
    ~EventConnection() { disconnect(); }

    EventConnection(EventConnection &&other) noexcept
        : m_signal(other.m_signal), m_id(other.m_id), m_alive(std::move(other.m_alive))
    {
        other.m_signal = nullptr;
    }

    EventConnection &operator=(EventConnection &&other) noexcept
    {
        if (this != &other) {
            disconnect();
            m_signal = other.m_signal;
            m_id = other.m_id;
            m_alive = std::move(other.m_alive);
            other.m_signal = nullptr;
        }
        return *this;
    }

    EventConnection(const EventConnection &) = delete;
    EventConnection &operator=(const EventConnection &) = delete;

    /**
     * @brief Disconnects from the signal.
     * Safe to call multiple times.
     */
    void disconnect();

    /**
     * @brief Returns whether this connection is still active.
     * @return True if connected, false otherwise.
     */
    bool connected() const
    {
        if (!m_signal) return false;
        auto alive = m_alive.lock();
        return alive && *alive;
    }

  private:
    EventConnection(EventSignalBase *signal, uint32_t id, std::weak_ptr<bool> alive)
        : m_signal(signal), m_id(id), m_alive(std::move(alive))
    {
    }

    EventSignalBase *m_signal = nullptr;
    uint32_t m_id = 0;
    std::weak_ptr<bool> m_alive;
};

inline void EventConnection::disconnect()
{
    if (m_signal) {
        auto alive = m_alive.lock();
        if (alive && *alive) m_signal->removeConnection(m_id);
        m_signal = nullptr;
    }
}

template <typename sig> class EventSignal;

template <typename... Args> class EventSignal<void(Args...)> : public EventSignalBase {
  public:
    /**
     * @brief Subscribes a callback to this signal.
     * Returns a scoped connection that automatically
     * disconnects on destruction.
     * @param func The callback to subscribe.
     * @return An EventConnection that manages the subscription lifetime.
     */
    EventConnection connect(std::function<void(Args...)> func)
    {
        uint32_t id = m_nextId++;
        m_slots.emplace(id, Slot{std::move(func), false});
        return EventConnection(this, id, m_alive);
    }

    /**
     * @brief Subscribes a callback that will be automatically
     * removed after the first fire.
     * @param func The callback to subscribe.
     * @return An EventConnection that manages the subscription lifetime.
     */
    EventConnection once(std::function<void(Args...)> func)
    {
        uint32_t id = m_nextId++;
        m_slots.emplace(id, Slot{std::move(func), true});
        return EventConnection(this, id, m_alive);
    }

    /**
     * @brief Subscribes a one-shot callback owned by the signal. No connection is
     * returned to manage; the slot is removed after it fires or when the signal dies.
     * Use when the subscriber has no safe lifetime to hold an EventConnection.
     * @param func The callback to subscribe.
     */
    void detachedOnce(std::function<void(Args...)> func)
    {
        m_slots.emplace(m_nextId++, Slot{std::move(func), true});
    }

    /**
     * @brief Invokes all connected callbacks with the given arguments.
     * Once-connections are removed after all callbacks have been called.
     * @param args The arguments to forward to each callback.
     */
    void fire(Args... args)
    {
        m_firing = true;
        for (auto &[id, slot] : m_slots) {
            if (!slot.pendingRemove) {
                slot.func(args...);
            }
        }
        std::erase_if(m_slots, [](const auto &pair) { return pair.second.once || pair.second.pendingRemove; });
        m_firing = false;
    }

    /**
     * @brief Removes a connection by its ID.
     * @param id The connection ID to remove.
     */
    void removeConnection(uint32_t id) override
    {
        if (m_firing) {
            auto it = m_slots.find(id);
            if (it != m_slots.end()) {
                it->second.pendingRemove = true;
            }
        } else {
            m_slots.erase(id);
        }
    }

  private:
    struct Slot {
        std::function<void(Args...)> func;
        bool once = false;
        bool pendingRemove = false;
    };

    std::map<uint32_t, Slot> m_slots;
    uint32_t m_nextId = 0;
};

} // namespace Rapture

#endif // RAPTURE__EVENT_SIGNAL_H

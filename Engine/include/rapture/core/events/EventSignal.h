#ifndef RAPTURE__EVENT_SIGNAL_H
#define RAPTURE__EVENT_SIGNAL_H

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Rapture {

template <typename>
class EventSignal;

class EventSignalBase {
  public:
    EventSignalBase() = default;

    virtual ~EventSignalBase()
    {
        if (m_alive) {
            *m_alive = false;
        }
    }

    /**
     * @brief Releases a slot, which stops it being called even during a fire already under way
     * @param slot The slot to release
     * @param generation The generation the slot was taken in
     */
    virtual void removeConnection(uint32_t slot, uint32_t generation) = 0;

    /**
     * @brief Whether a slot still holds the callback it was taken for
     * @param slot The slot to test
     * @param generation The generation the slot was taken in
     * @return True if the callback is still subscribed
     */
    virtual bool isConnected(uint32_t slot, uint32_t generation) const = 0;

  protected:
    /**
     * @brief The flag a connection reads to tell whether this signal still exists
     * @return The flag, created on the first call
     */
    const std::shared_ptr<bool> &aliveFlag()
    {
        if (!m_alive) {
            m_alive = std::make_shared<bool>(true);
        }
        return m_alive;
    }

  private:
    std::shared_ptr<bool> m_alive;
};

class EventConnection {
    template <typename>
    friend class EventSignal;

  public:
    EventConnection() = default;

    /**
     * @brief Destructor automatically disconnects from the
     * signal if still connected.
     */
    ~EventConnection() { disconnect(); }

    EventConnection(EventConnection &&other) noexcept
        : m_signal(other.m_signal), m_slot(other.m_slot), m_generation(other.m_generation), m_alive(std::move(other.m_alive))
    {
        other.m_signal = nullptr;
    }

    EventConnection &operator=(EventConnection &&other) noexcept
    {
        if (this != &other) {
            disconnect();
            m_signal = other.m_signal;
            m_slot = other.m_slot;
            m_generation = other.m_generation;
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
    bool connected() const;

  private:
    EventConnection(EventSignalBase *signal, uint32_t slot, uint32_t generation, std::weak_ptr<bool> alive)
        : m_signal(signal), m_slot(slot), m_generation(generation), m_alive(std::move(alive))
    {
    }

    EventSignalBase *m_signal = nullptr;
    uint32_t m_slot = 0;
    uint32_t m_generation = 0;
    std::weak_ptr<bool> m_alive;
};

inline void EventConnection::disconnect()
{
    if (m_signal != nullptr) {
        auto alive = m_alive.lock();
        if (alive && *alive) {
            m_signal->removeConnection(m_slot, m_generation);
        }
        m_signal = nullptr;
    }
}

inline bool EventConnection::connected() const
{
    if (m_signal == nullptr) {
        return false;
    }

    auto alive = m_alive.lock();
    if (!alive || !*alive) {
        return false;
    }
    return m_signal->isConnected(m_slot, m_generation);
}

/**
 * @brief A callback list another object subscribes to and this one calls.
 *
 * A slot released while a fire is under way stops being called immediately, and one taken during a
 * fire is not called until the next one, so a callback is free to connect and disconnect anything
 * including itself. Fires may nest.
 */
template <typename... Args>
class EventSignal<void(Args...)> : public EventSignalBase {
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
        uint32_t slot = takeSlot(std::move(func), false);
        return EventConnection(this, slot, m_slots[slot].generation, aliveFlag());
    }

    /**
     * @brief Subscribes a callback that will be automatically
     * removed after the first fire.
     * @param func The callback to subscribe.
     * @return An EventConnection that manages the subscription lifetime.
     */
    EventConnection once(std::function<void(Args...)> func)
    {
        uint32_t slot = takeSlot(std::move(func), true);
        return EventConnection(this, slot, m_slots[slot].generation, aliveFlag());
    }

    /**
     * @brief Subscribes a one-shot callback owned by the signal. No connection is
     * returned to manage; the slot is removed after it fires or when the signal dies.
     * Use when the subscriber has no safe lifetime to hold an EventConnection.
     * @param func The callback to subscribe.
     */
    void detachedOnce(std::function<void(Args...)> func) { takeSlot(std::move(func), true); }

    /**
     * @brief Invokes every callback subscribed before this call was made
     * @param args The arguments to forward to each callback.
     */
    void fire(Args... args)
    {
        const uint64_t barrier = m_nextSequence;

        ++m_fireDepth;
        for (uint32_t slot = 0; slot < static_cast<uint32_t>(m_slots.size()); ++slot) {
            // re-read rather than holding a reference, since a callback may take a slot and grow the deque
            if (!isCallable(m_slots[slot], barrier)) {
                continue;
            }

            m_slots[slot].func(args...);

            if (m_slots[slot].once && !m_slots[slot].pendingRemove) {
                m_slots[slot].pendingRemove = true;
                m_pendingRemoves.push_back(slot);
            }
        }
        --m_fireDepth;

        if (m_fireDepth == 0) {
            reclaim();
        }
    }

    void removeConnection(uint32_t slot, uint32_t generation) override
    {
        if (!isLive(slot, generation) || m_slots[slot].pendingRemove) {
            return;
        }

        m_slots[slot].pendingRemove = true;
        m_pendingRemoves.push_back(slot);

        if (m_fireDepth == 0) {
            reclaim();
        }
    }

    bool isConnected(uint32_t slot, uint32_t generation) const override
    {
        return isLive(slot, generation) && !m_slots[slot].pendingRemove;
    }

    /**
     * @brief The number of callbacks that would be invoked by a fire
     */
    uint32_t connectionCount() const { return m_liveCount - static_cast<uint32_t>(m_pendingRemoves.size()); }

  private:
    struct Slot {
        std::function<void(Args...)> func;
        uint64_t sequence = 0;
        uint32_t generation = 0;
        bool live = false;
        bool once = false;
        bool pendingRemove = false;
    };

    bool isLive(uint32_t slot, uint32_t generation) const
    {
        return slot < m_slots.size() && m_slots[slot].live && m_slots[slot].generation == generation;
    }

    bool isCallable(const Slot &slot, uint64_t barrier) const
    {
        return slot.live && !slot.pendingRemove && slot.sequence < barrier;
    }

    /**
     * @brief Puts a callback in a free slot, or in a new one
     * @param func The callback to store
     * @param once Whether the slot is released after it is called
     * @return The slot the callback was put in
     */
    uint32_t takeSlot(std::function<void(Args...)> func, bool once)
    {
        uint32_t slot;
        if (!m_freeSlots.empty()) {
            slot = m_freeSlots.back();
            m_freeSlots.pop_back();
        } else {
            slot = static_cast<uint32_t>(m_slots.size());
            m_slots.emplace_back();
        }

        Slot &taken = m_slots[slot];
        taken.func = std::move(func);
        taken.sequence = m_nextSequence++;
        taken.live = true;
        taken.once = once;
        taken.pendingRemove = false;
        ++m_liveCount;

        return slot;
    }

    /**
     * @brief Frees every slot released since the last reclaim, only called outside a fire
     */
    void reclaim()
    {
        for (uint32_t slot : m_pendingRemoves) {
            Slot &released = m_slots[slot];
            released.func = nullptr;
            released.live = false;
            released.pendingRemove = false;
            ++released.generation;
            m_freeSlots.push_back(slot);
            --m_liveCount;
        }
        m_pendingRemoves.clear();
    }

    // a deque because a callback may connect during a fire, and the slot being called has to keep its address
    std::deque<Slot> m_slots;
    std::vector<uint32_t> m_freeSlots;
    std::vector<uint32_t> m_pendingRemoves;
    uint64_t m_nextSequence = 1;
    uint32_t m_liveCount = 0;
    uint32_t m_fireDepth = 0;
};

} // namespace Rapture

#endif // RAPTURE__EVENT_SIGNAL_H

#ifndef RAPTURE__COMPONENT_SIGNAL_H
#define RAPTURE__COMPONENT_SIGNAL_H

#include "common.h"

#include <functional>
#include <memory>
#include <vector>

namespace Rapture {
namespace ecs {

class Registry;
class ComponentSignal;

/**
 * @brief RAII handle to one subscription, disconnects when it goes out of scope.
 *
 * Outliving the signal is safe, the handle notices and does nothing.
 */
class SignalConnection {
  public:
    SignalConnection() = default;
    SignalConnection(ComponentSignal *signal, std::weak_ptr<void> alive, uint32_t slotId);
    ~SignalConnection();

    SignalConnection(const SignalConnection &) = delete;
    SignalConnection &operator=(const SignalConnection &) = delete;

    SignalConnection(SignalConnection &&other) noexcept;
    SignalConnection &operator=(SignalConnection &&other) noexcept;

    void disconnect();

    bool isConnected() const;

  private:
    ComponentSignal *m_signal = nullptr;
    std::weak_ptr<void> m_alive;
    uint32_t m_slotId = 0;
};

/**
 * @brief Lifetime callbacks for one component type on one pool.
 */
class ComponentSignal {
  public:
    using Callback = std::function<void(Registry &registry, Entity entity)>;

    /**
     * @brief Subscribes to this signal.
     * @param callback Handler to run, receiving the registry and the entity.
     * @return Connection that unsubscribes when destroyed.
     */
    SignalConnection connect(Callback callback);

    /**
     * @brief Unsubscribes a slot, deferring the erase if a fire is in progress.
     * @param slotId Slot to drop.
     */
    void disconnect(uint32_t slotId);

    /**
     * @brief Runs every handler.
     * @param registry Registry the entity belongs to.
     * @param entity Entity the component was attached to or is about to leave.
     */
    void fire(Registry &registry, Entity entity);

    bool isEmpty() const;

    /**
     * @brief Token the connections watch to tell whether this signal still exists.
     * @return Shared token, expired once the signal is destroyed.
     */
    std::weak_ptr<void> getAliveToken() const;

  private:
    void compact();

  private:
    struct Slot {
        uint32_t id = 0;
        Callback callback;
    };

    std::vector<Slot> m_slots;
    std::shared_ptr<uint8_t> m_alive = std::make_shared<uint8_t>();
    uint32_t m_nextSlotId = 1;
    uint32_t m_fireDepth = 0;
    bool m_needsCompact = false;
};

} // namespace ecs
} // namespace Rapture

#endif // RAPTURE__COMPONENT_SIGNAL_H

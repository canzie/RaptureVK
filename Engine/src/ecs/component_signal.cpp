#include "component_signal.h"

namespace Rapture {
namespace ecs {

SignalConnection::SignalConnection(ComponentSignal *signal, std::weak_ptr<void> alive, uint32_t slotId)
    : m_signal(signal), m_alive(std::move(alive)), m_slotId(slotId)
{
}

SignalConnection::~SignalConnection()
{
    disconnect();
}

SignalConnection::SignalConnection(SignalConnection &&other) noexcept
    : m_signal(other.m_signal), m_alive(std::move(other.m_alive)), m_slotId(other.m_slotId)
{
    other.m_signal = nullptr;
    other.m_slotId = 0;
}

SignalConnection &SignalConnection::operator=(SignalConnection &&other) noexcept
{
    if (this != &other) {
        disconnect();

        m_signal = other.m_signal;
        m_alive = std::move(other.m_alive);
        m_slotId = other.m_slotId;

        other.m_signal = nullptr;
        other.m_slotId = 0;
    }
    return *this;
}

void SignalConnection::disconnect()
{
    if (isConnected()) {
        m_signal->disconnect(m_slotId);
    }

    m_signal = nullptr;
    m_alive.reset();
    m_slotId = 0;
}

bool SignalConnection::isConnected() const
{
    return m_signal != nullptr && !m_alive.expired();
}

SignalConnection ComponentSignal::connect(Callback callback)
{
    if (m_fireDepth == 0 && m_needsCompact) {
        compact();
    }

    uint32_t slotId = m_nextSlotId++;
    m_slots.push_back(Slot{slotId, std::move(callback)});

    return SignalConnection(this, m_alive, slotId);
}

void ComponentSignal::disconnect(uint32_t slotId)
{
    for (auto &slot : m_slots) {
        if (slot.id == slotId) {
            slot.id = 0;
            slot.callback = nullptr;
            m_needsCompact = true;
            break;
        }
    }

    if (m_fireDepth == 0) {
        compact();
    }
}

void ComponentSignal::fire(Registry &registry, Entity entity)
{
    m_fireDepth++;

    size_t count = m_slots.size();
    for (size_t i = 0; i < count; i++) {
        if (m_slots[i].callback != nullptr) {
            m_slots[i].callback(registry, entity);
        }
    }

    m_fireDepth--;

    if (m_fireDepth == 0 && m_needsCompact) {
        compact();
    }
}

bool ComponentSignal::isEmpty() const
{
    return m_slots.empty();
}

std::weak_ptr<void> ComponentSignal::getAliveToken() const
{
    return m_alive;
}

void ComponentSignal::compact()
{
    std::erase_if(m_slots, [](const Slot &slot) { return slot.callback == nullptr; });
    m_needsCompact = false;
}

} // namespace ecs
} // namespace Rapture

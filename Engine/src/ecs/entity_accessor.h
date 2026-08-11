#ifndef RAPTURE__ENTITY_ACCESSOR_H
#define RAPTURE__ENTITY_ACCESSOR_H

#include "registry.h"

namespace Rapture {
namespace ecs {

/**
 * @brief An entity paired with the registry that resolves it.
 *
 * Made at a call site that already holds a registry, used, and dropped. Anything stored per
 * entity stores the Entity itself.
 */
class EntityAccessor {
  public:
    EntityAccessor() = default;
    EntityAccessor(Entity entity, Registry *registry) : m_entity(entity), m_registry(registry) {}

    bool isValid() const { return m_registry != nullptr && m_registry->isValid(m_entity); }

    Entity getEntity() const { return m_entity; }

    bool operator==(const EntityAccessor &other) const
    {
        return m_entity == other.m_entity && m_registry == other.m_registry;
    }

    /**
     * @brief Attaches a component to this entity.
     * @param args Arguments forwarded to T's constructor.
     * @return Reference to the new component, void for empty components.
     */
    template <typename T, typename... Args>
    decltype(auto) add(Args &&...args)
    {
        return m_registry->add<T>(m_entity, std::forward<Args>(args)...);
    }

    /**
     * @brief Attaches a component, replacing it if this entity already has one.
     * @param args Arguments forwarded to T's constructor.
     * @return Reference to the component, void for empty components.
     */
    template <typename T, typename... Args>
    decltype(auto) set(Args &&...args)
    {
        return m_registry->set<T>(m_entity, std::forward<Args>(args)...);
    }

    template <typename T>
    void remove()
    {
        m_registry->remove<T>(m_entity);
    }

    /**
     * @brief Detaches a component this entity may not have.
     * @return True if a component was removed.
     */
    template <typename T>
    bool tryRemove()
    {
        return m_registry != nullptr && m_registry->tryRemove<T>(m_entity);
    }

    template <typename T>
    bool has() const
    {
        return m_registry != nullptr && m_registry->has<T>(m_entity);
    }

    template <typename... Ts>
    bool hasAll() const
    {
        return m_registry != nullptr && m_registry->hasAll<Ts...>(m_entity);
    }

    template <typename... Ts>
    bool hasAny() const
    {
        return m_registry != nullptr && m_registry->hasAny<Ts...>(m_entity);
    }

    /**
     * @brief Immutable access to a component this entity is known to hold.
     * @return Const reference to the component.
     */
    template <typename T>
    const T &read() const
    {
        return m_registry->read<T>(m_entity);
    }

    /**
     * @brief Immutable access to a component this entity may not hold.
     * @return Const pointer to the component, or nullptr if it has none.
     */
    template <typename T>
    const T *tryRead() const
    {
        if (m_registry == nullptr) {
            return nullptr;
        }
        return m_registry->tryRead<T>(m_entity);
    }

    /**
     * @brief Mutable access to a component this entity is known to hold.
     * @return Scope that records the component's declared channels when it ends.
     */
    template <typename T>
    WriteScope<T> write() const
    {
        return m_registry->write<T>(m_entity);
    }

    /**
     * @brief Mutable access recording something other than the component's declared channels.
     * @param channels Channels to record on when the scope ends.
     * @return Scope over the component.
     */
    template <typename T>
    WriteScope<T> write(ChangeMask channels) const
    {
        return m_registry->write<T>(m_entity, channels);
    }

  private:
    Entity m_entity = ENTITY_NULL;
    Registry *m_registry = nullptr;
};

} // namespace ecs
} // namespace Rapture

#endif // RAPTURE__ENTITY_ACCESSOR_H

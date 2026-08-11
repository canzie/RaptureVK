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

    template <typename T>
    void remove()
    {
        m_registry->remove<T>(m_entity);
    }

    template <typename T>
    bool has() const
    {
        return m_registry != nullptr && m_registry->has<T>(m_entity);
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
     * @return Reference to the component.
     */
    template <typename T>
    T &write()
    {
        return m_registry->write<T>(m_entity);
    }

  private:
    Entity m_entity = ENTITY_NULL;
    Registry *m_registry = nullptr;
};

} // namespace ecs
} // namespace Rapture

#endif // RAPTURE__ENTITY_ACCESSOR_H

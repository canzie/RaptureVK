#ifndef RAPTURE__REGISTRY_H
#define RAPTURE__REGISTRY_H

#include "component_pool.h"
#include "view.h"
#include "write_scope.h"

#include <memory>
#include <vector>

namespace Rapture {
namespace ecs {

/**
 * @brief Owns the entities and the component pools.
 */
class Registry {
  public:
    /**
     * @brief Builds a registry and the journal its writes are recorded in.
     * @param changeChannelCount Number of change channels the engine declares, zero to not track.
     */
    explicit Registry(uint32_t changeChannelCount = 0) : m_journal(changeChannelCount) {}

    ~Registry() = default;

    Registry(const Registry &) = delete;
    Registry &operator=(const Registry &) = delete;

    /**
     * @brief Creates an entity, reusing a destroyed slot when one is free.
     * @return The new entity.
     */
    Entity create();

    /**
     * @brief Destroys an entity and every component attached to it.
     * @param entity Entity to destroy, must be alive.
     */
    void destroy(Entity entity);

    /**
     * @brief Tests whether an entity is alive.
     * @param entity Entity to test, may be stale or malformed.
     * @return True if the entity is alive and its generation still matches.
     */
    bool isValid(Entity entity) const;

    void clear();

    uint32_t getAliveCount() const;

    /**
     * @brief Component mask of an entity, one bit per component type it holds.
     * @param entity Entity to inspect, must be alive.
     * @return The mask.
     */
    ComponentMask getComponentMask(Entity entity) const;

    /**
     * @brief Attaches a component to an entity that does not have one.
     * @param entity Entity to attach to, must be alive.
     * @param args Arguments forwarded to T's constructor.
     * @return Reference to the new component, void for empty components.
     */
    template <typename T, typename... Args>
    decltype(auto) add(Entity entity, Args &&...args)
    {
        RP_ASSERT(isValid(entity), "cannot add a component to a dead entity");
        RP_ASSERT(!has<T>(entity), "entity already has this component");

        m_records[EntityIndex(entity)].components |= ComponentBit<T>();

        ComponentPool<T> &pool = assurePool<T>();
        pool.emplace(entity, std::forward<Args>(args)...);
        pool.getConstructSignal().fire(*this, entity);

        if constexpr (!ComponentPool<T>::IS_EMPTY) {
            return pool.get(entity);
        }
    }

    /**
     * @brief Detaches a component from an entity.
     * @param entity Entity to detach from, must have the component.
     */
    template <typename T>
    void remove(Entity entity)
    {
        RP_ASSERT(has<T>(entity), "entity does not have this component");

        ComponentPool<T> *pool = getPool<T>();
        pool->getDestroySignal().fire(*this, entity);

        m_records[EntityIndex(entity)].components &= ~ComponentBit<T>();
        pool->remove(entity);
    }

    /**
     * @brief Subscribes to T being attached to any entity, after the entity is complete.
     * @param callback Handler receiving the registry and the entity.
     * @return Connection that unsubscribes when destroyed.
     */
    template <typename T>
    SignalConnection onConstruct(ComponentSignal::Callback callback)
    {
        return assurePool<T>().getConstructSignal().connect(std::move(callback));
    }

    /**
     * @brief Subscribes to T leaving any entity, while the component can still be read.
     * @param callback Handler receiving the registry and the entity.
     * @return Connection that unsubscribes when destroyed.
     */
    template <typename T>
    SignalConnection onDestroy(ComponentSignal::Callback callback)
    {
        return assurePool<T>().getDestroySignal().connect(std::move(callback));
    }

    /**
     * @brief Tests whether an entity holds a component.
     * @param entity Entity to test.
     * @return True if the entity is alive and holds T.
     */
    template <typename T>
    bool has(Entity entity) const
    {
        if (!isValid(entity)) {
            return false;
        }
        return (m_records[EntityIndex(entity)].components & ComponentBit<T>()) != 0;
    }

    /**
     * @brief Tests whether an entity holds every listed component.
     * @param entity Entity to test.
     * @return True if the entity is alive and holds all of Ts.
     */
    template <typename... Ts>
    bool hasAll(Entity entity) const
    {
        if (!isValid(entity)) {
            return false;
        }
        ComponentMask required = ComponentBits<Ts...>();
        return (m_records[EntityIndex(entity)].components & required) == required;
    }

    /**
     * @brief Immutable access to a component the entity is known to hold.
     * @param entity Entity to read from.
     * @return Const reference to its component.
     */
    template <typename T>
    const T &read(Entity entity) const
    {
        const ComponentPool<T> *pool = getPool<T>();
        RP_ASSERT(pool != nullptr && pool->contains(entity), "entity does not have this component");
        return pool->get(entity);
    }

    /**
     * @brief Immutable access to a component the entity may not hold.
     * @param entity Entity to read from.
     * @return Const pointer to its component, or nullptr if it has none.
     */
    template <typename T>
    const T *tryRead(Entity entity) const
    {
        const ComponentPool<T> *pool = getPool<T>();
        if (pool == nullptr) {
            return nullptr;
        }
        return pool->tryGet(entity);
    }

    /**
     * @brief Mutable access to a component the entity is known to hold.
     * @param entity Entity to write to.
     * @return Scope that records the component's declared channels when it ends.
     */
    template <typename T>
    WriteScope<T> write(Entity entity)
    {
        return write<T>(entity, ComponentTraits<T>::CHANGE_CHANNELS);
    }

    /**
     * @brief Mutable access recording something other than the component's declared channels.
     * @param entity Entity to write to.
     * @param channels Channels to record on when the scope ends.
     * @return Scope over the component.
     */
    template <typename T>
    WriteScope<T> write(Entity entity, ChangeMask channels)
    {
        ComponentPool<T> *pool = getPool<T>();
        RP_ASSERT(pool != nullptr && pool->contains(entity), "entity does not have this component");
        return WriteScope<T>(&pool->get(entity), &m_journal, entity, channels);
    }

    Journal &getJournal() { return m_journal; }

    /**
     * @brief Pool holding every instance of a component type.
     * @return The pool, or nullptr if no entity has ever held T.
     */
    template <typename T>
    ComponentPool<T> *getPool()
    {
        ComponentTypeId typeId = ComponentType<T>();
        if (typeId >= m_pools.size() || m_pools[typeId] == nullptr) {
            return nullptr;
        }
        return static_cast<ComponentPool<T> *>(m_pools[typeId].get());
    }

    template <typename T>
    const ComponentPool<T> *getPool() const
    {
        ComponentTypeId typeId = ComponentType<T>();
        if (typeId >= m_pools.size() || m_pools[typeId] == nullptr) {
            return nullptr;
        }
        return static_cast<const ComponentPool<T> *>(m_pools[typeId].get());
    }

    /**
     * @brief View over every entity holding all of Ts.
     * @return A view yielding const references, empty if no entity holds all of Ts.
     */
    template <typename... Ts>
    View<Ts...> read() const
    {
        return View<Ts...>(&m_records, getPool<Ts>()...);
    }

    /**
     * @brief View over every entity holding all of Ts, yielding mutable references.
     * @return A view that records every entity it yields on the channels Ts declare.
     */
    template <typename... Ts>
    MutableView<Ts...> mutableView()
    {
        return MutableView<Ts...>(&m_records, &m_journal, getPool<Ts>()...);
    }

    /**
     * @brief Entity records, so a view can test a candidate against its component mask.
     * @return The record array, indexed by entity index.
     */
    const std::vector<EntityRecord> &getRecords() const;

  private:
    /**
     * @brief Pool for a component type, creating it if this is the first use.
     * @return The pool.
     */
    template <typename T>
    ComponentPool<T> &assurePool()
    {
        ComponentTypeId typeId = ComponentType<T>();
        if (typeId >= m_pools.size()) {
            m_pools.resize(typeId + 1);
        }
        if (m_pools[typeId] == nullptr) {
            m_pools[typeId] = std::make_unique<ComponentPool<T>>();
        }
        return *static_cast<ComponentPool<T> *>(m_pools[typeId].get());
    }

  private:
    std::vector<EntityRecord> m_records;
    std::vector<std::unique_ptr<ComponentPoolBase>> m_pools;
    Journal m_journal;
    uint32_t m_freeHead = ENTITY_FREE_LIST_END;
    uint32_t m_aliveCount = 0;
};

} // namespace ecs
} // namespace Rapture

#endif // RAPTURE__REGISTRY_H

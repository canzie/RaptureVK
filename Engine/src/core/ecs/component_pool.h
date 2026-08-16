#ifndef RAPTURE__COMPONENT_POOL_H
#define RAPTURE__COMPONENT_POOL_H

#include "component_signal.h"
#include "sparse_set.h"

#include "core/utils/rp_assert.h"

#include <type_traits>
#include <utility>
#include <vector>

namespace Rapture {
namespace ecs {

/**
 * @brief Hands out the next unused component type id.
 * @return A type id below COMPONENT_TYPE_MAX.
 */
inline ComponentTypeId NextComponentTypeId()
{
    static ComponentTypeId s_next = 0;
    ComponentTypeId id = s_next++;
    RP_ASSERT(id < COMPONENT_TYPE_MAX, "component type limit reached, ComponentMask needs a second word");
    return id;
}

/**
 * @brief Type id of a component type, assigned on first use and stable for one run only.
 * @return The type id of T.
 */
template <typename T>
ComponentTypeId ComponentType()
{
    static const ComponentTypeId s_id = NextComponentTypeId();
    return s_id;
}

/**
 * @brief Mask bit identifying a component type in an entity record.
 * @return The mask with only T's bit set.
 */
template <typename T>
ComponentMask ComponentBit()
{
    return ComponentMask(1) << ComponentType<T>();
}

/**
 * @brief Mask with the bit of every listed component type set.
 * @return The combined mask.
 */
template <typename... Ts>
ComponentMask ComponentBits()
{
    return (ComponentMask(0) | ... | ComponentBit<Ts>());
}

/**
 * @brief Type erased pool interface, so the registry can hold pools of every component type.
 */
class ComponentPoolBase {
  public:
    virtual ~ComponentPoolBase() = default;

    virtual bool contains(Entity entity) const = 0;
    virtual void remove(Entity entity) = 0;
    virtual void clear() = 0;
    virtual uint32_t getSize() const = 0;
    virtual const std::vector<Entity> &getEntities() const = 0;

    virtual ComponentSignal &getConstructSignal() = 0;
    virtual ComponentSignal &getDestroySignal() = 0;
};

/**
 * @brief Storage for one component type, a sparse set paired with a packed data array.
 *
 * Empty components carry no data array at all, so a tag pool is nothing but membership.
 */
template <typename T>
class ComponentPool final : public ComponentPoolBase {
  public:
    static constexpr bool IS_EMPTY = std::is_empty_v<T>;

    /**
     * @brief Constructs a component for an entity that does not have one yet.
     * @param entity Entity to attach to.
     * @param args Arguments forwarded to T's constructor.
     * @return Reference to the new component, void for empty components.
     */
    template <typename... Args>
    decltype(auto) emplace(Entity entity, Args &&...args)
    {
        uint32_t denseIndex = m_entities.insert(entity);

        if constexpr (IS_EMPTY) {
            (void)denseIndex;
            return;
        } else {
            RP_ASSERT(denseIndex == m_data.size(), "sparse set and data array are out of step");
            return m_data.emplace_back(std::forward<Args>(args)...);
        }
    }

    /**
     * @brief Component of an entity that is known to have one.
     * @param entity Entity to look up.
     * @return Reference to its component.
     */
    T &get(Entity entity)
    requires(!IS_EMPTY)
    {
        return m_data[m_entities.getDenseIndex(entity)];
    }

    const T &get(Entity entity) const
    requires(!IS_EMPTY)
    {
        return m_data[m_entities.getDenseIndex(entity)];
    }

    /**
     * @brief Component of an entity that may not have one.
     * @param entity Entity to look up.
     * @return Pointer to its component, or nullptr if it has none.
     */
    T *tryGet(Entity entity)
    requires(!IS_EMPTY)
    {
        if (!m_entities.contains(entity)) {
            return nullptr;
        }
        return &m_data[m_entities.getDenseIndex(entity)];
    }

    const T *tryGet(Entity entity) const
    requires(!IS_EMPTY)
    {
        if (!m_entities.contains(entity)) {
            return nullptr;
        }
        return &m_data[m_entities.getDenseIndex(entity)];
    }

    /**
     * @brief Component at a packed position, for iteration that already resolved the index.
     * @param denseIndex Position in the packed array.
     * @return Reference to the component.
     */
    T &atDense(uint32_t denseIndex)
    requires(!IS_EMPTY)
    {
        return m_data[denseIndex];
    }

    const T &atDense(uint32_t denseIndex) const
    requires(!IS_EMPTY)
    {
        return m_data[denseIndex];
    }

    bool contains(Entity entity) const override { return m_entities.contains(entity); }

    void remove(Entity entity) override
    {
        uint32_t vacated = m_entities.remove(entity);

        if constexpr (IS_EMPTY) {
            (void)vacated;
        } else {
            uint32_t last = static_cast<uint32_t>(m_data.size()) - 1;
            if (vacated != last) {
                m_data[vacated] = std::move(m_data[last]);
            }
            m_data.pop_back();
        }
    }

    void clear() override
    {
        m_entities.clear();
        if constexpr (!IS_EMPTY) {
            m_data.clear();
        }
    }

    uint32_t getSize() const override { return m_entities.getSize(); }

    const std::vector<Entity> &getEntities() const override { return m_entities.getEntities(); }

    ComponentSignal &getConstructSignal() override { return m_onConstruct; }

    ComponentSignal &getDestroySignal() override { return m_onDestroy; }

  private:
    struct NoData {};

    SparseSet m_entities;
    ComponentSignal m_onConstruct;
    ComponentSignal m_onDestroy;
    [[no_unique_address]] std::conditional_t<IS_EMPTY, NoData, std::vector<T>> m_data;
};

} // namespace ecs
} // namespace Rapture

#endif // RAPTURE__COMPONENT_POOL_H

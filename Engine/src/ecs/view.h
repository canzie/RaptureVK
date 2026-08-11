#ifndef RAPTURE__VIEW_H
#define RAPTURE__VIEW_H

#include "component_pool.h"
#include "write_scope.h"

#include <tuple>

namespace Rapture {
namespace ecs {

/**
 * @brief Iterates every entity holding all of Ts.
 *
 * The smallest of the requested pools drives the loop and the entity's component mask decides
 * membership in one test, so extra required or forbidden types cost nothing per candidate.
 * A mutable view records every entity it yields on the channels its component types declare.
 */
template <bool MUTABLE, typename... Ts>
class BasicView {
  public:
    static_assert((!std::is_empty_v<Ts> && ...), "empty components carry no data, filter with with() instead");

    template <typename T>
    using PoolPointer = std::conditional_t<MUTABLE, ComponentPool<T> *, const ComponentPool<T> *>;

    template <typename T>
    using Reference = std::conditional_t<MUTABLE, T &, const T &>;

    using ValueType = std::tuple<Entity, Reference<Ts>...>;

    /**
     * @brief Binds a read only view to the pools it walks and the records it filters on.
     * @param records Entity record array of the owning registry.
     * @param pools One pool per requested component type, any of which may be nullptr.
     */
    BasicView(const std::vector<EntityRecord> *records, PoolPointer<Ts>... pools)
    requires(!MUTABLE)
        : m_pools(pools...), m_records(records), m_include(ComponentBits<Ts...>())
    {
        selectDriver(pools...);
    }

    /**
     * @brief Binds a mutable view, which records every entity it yields.
     * @param records Entity record array of the owning registry.
     * @param journal Journal the yielded entities are recorded in.
     * @param pools One pool per requested component type, any of which may be nullptr.
     */
    BasicView(const std::vector<EntityRecord> *records, Journal *journal, PoolPointer<Ts>... pools)
    requires(MUTABLE)
        : m_pools(pools...), m_records(records), m_journal(journal), m_include(ComponentBits<Ts...>())
    {
        selectDriver(pools...);
    }

    /**
     * @brief Requires a component the view does not access, such as a tag.
     * @return A copy of this view with the filter applied, safe to iterate as a temporary.
     */
    template <typename... Fs>
    BasicView with() const
    {
        BasicView filtered = *this;
        filtered.m_include |= ComponentBits<Fs...>();
        return filtered;
    }

    /**
     * @brief Rejects entities holding any of the listed components.
     * @return A copy of this view with the filter applied, safe to iterate as a temporary.
     */
    template <typename... Fs>
    BasicView without() const
    {
        BasicView filtered = *this;
        filtered.m_exclude |= ComponentBits<Fs...>();
        return filtered;
    }

    class Iterator {
      public:
        Iterator(const BasicView *view, uint32_t index) : m_view(view), m_index(index) { seekMatch(); }

        Iterator &operator++()
        {
            m_index++;
            seekMatch();
            return *this;
        }

        bool operator!=(const Iterator &other) const { return m_index != other.m_index; }

        ValueType operator*() const
        {
            Entity entity = (*m_view->m_driver)[m_index];

            // a single pool is always the driver, so its dense position is the one being iterated
            if constexpr (sizeof...(Ts) == 1) {
                return ValueType(entity, std::get<0>(m_view->m_pools)->atDense(m_index));
            } else {
                return std::apply([entity](auto *...pools) { return ValueType(entity, pools->get(entity)...); }, m_view->m_pools);
            }
        }

      private:
        void seekMatch()
        {
            while (m_index < m_view->m_driverSize && !m_view->matches((*m_view->m_driver)[m_index])) {
                m_index++;
            }

            if constexpr (MUTABLE) {
                if (m_index < m_view->m_driverSize) {
                    m_view->m_journal->record((*m_view->m_driver)[m_index], CHANGE_CHANNELS);
                }
            }
        }

        const BasicView *m_view;
        uint32_t m_index;
    };

    Iterator begin() const { return Iterator(this, 0); }

    Iterator end() const { return Iterator(this, m_driverSize); }

  private:
    static constexpr ChangeMask CHANGE_CHANNELS = (ChangeMask(0) | ... | COMPONENT_CHANNELS<Ts>);

    /**
     * @brief Picks the smallest pool to drive iteration, or none if any pool is missing.
     * @param pools The pools the view was built from.
     */
    void selectDriver(PoolPointer<Ts>... pools)
    {
        if (((pools == nullptr) || ...)) {
            return;
        }

        const ComponentPoolBase *smallest = nullptr;
        (
            [&] {
                if (smallest == nullptr || pools->getSize() < smallest->getSize()) {
                    smallest = pools;
                }
            }(),
            ...);

        m_driver = &smallest->getEntities();
        m_driverSize = smallest->getSize();
    }

    /**
     * @brief Tests a candidate entity against the required and forbidden masks.
     * @param entity Candidate produced by the driving pool.
     * @return True if the entity holds every required component and none of the forbidden ones.
     */
    bool matches(Entity entity) const
    {
        ComponentMask components = (*m_records)[EntityIndex(entity)].components;
        return (components & m_include) == m_include && (components & m_exclude) == 0;
    }

  private:
    std::tuple<PoolPointer<Ts>...> m_pools;
    const std::vector<EntityRecord> *m_records;
    Journal *m_journal = nullptr;
    const std::vector<Entity> *m_driver = nullptr;
    uint32_t m_driverSize = 0;
    ComponentMask m_include;
    ComponentMask m_exclude = 0;
};

template <typename... Ts>
using View = BasicView<false, Ts...>;

template <typename... Ts>
using MutableView = BasicView<true, Ts...>;

} // namespace ecs
} // namespace Rapture

#endif // RAPTURE__VIEW_H

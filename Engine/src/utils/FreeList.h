#ifndef RAPTURE__FREE_LIST_H
#define RAPTURE__FREE_LIST_H

#include <cstdint>
#include <utility>
#include <vector>

#include "utils/rp_assert.h"

namespace Rapture {

/**
 * @brief A slotted container with O(1) insert and remove and stable ids
 *
 * Ids are slot indices that stay valid for every element that is not removed, so external
 * references keep pointing at the same element after unrelated inserts and removes. Removed
 * slots are recycled, so an id may be handed out again after its element is removed. Iteration
 * over live elements is done with forEach.
 */
template <typename T> class FreeList {
  public:
    static constexpr uint32_t INVALID_ID = UINT32_MAX;

    /**
     * @brief Stores a value in a free slot
     * @param value The value to store
     * @return The id of the stored element
     */
    uint32_t insert(T value)
    {
        if (!m_freeSlots.empty()) {
            uint32_t id = m_freeSlots.back();
            m_freeSlots.pop_back();
            m_slots[id].value = std::move(value);
            m_slots[id].live = true;
            ++m_liveCount;
            return id;
        }
        m_slots.push_back(Slot{std::move(value), true});
        ++m_liveCount;
        return static_cast<uint32_t>(m_slots.size() - 1);
    }

    /**
     * @brief Removes the element at an id, freeing its slot for reuse
     * @param id The id to remove
     */
    void remove(uint32_t id)
    {
        if (!isLive(id)) {
            return;
        }
        m_slots[id].value = T{};
        m_slots[id].live = false;
        m_freeSlots.push_back(id);
        --m_liveCount;
    }

    /**
     * @brief Whether an id currently holds a live element
     * @param id The id to test
     * @return True if the slot is live
     */
    bool isLive(uint32_t id) const { return id < m_slots.size() && m_slots[id].live; }

    T &operator[](uint32_t id)
    {
        RP_ASSERT(isLive(id), "FreeList access of a dead or out of range id");
        return m_slots[id].value;
    }

    const T &operator[](uint32_t id) const
    {
        RP_ASSERT(isLive(id), "FreeList access of a dead or out of range id");
        return m_slots[id].value;
    }

    /**
     * @brief The number of live elements
     * @return The live element count
     */
    uint32_t size() const { return m_liveCount; }

    /**
     * @brief Removes every element
     */
    void clear()
    {
        m_slots.clear();
        m_freeSlots.clear();
        m_liveCount = 0;
    }

    /**
     * @brief Invokes a callback for each live element
     * @param fn Callable invoked as fn(uint32_t id, T& value)
     */
    template <typename F> void forEach(F &&fn)
    {
        for (uint32_t i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].live) {
                fn(i, m_slots[i].value);
            }
        }
    }

    /**
     * @brief Invokes a callback for each live element
     * @param fn Callable invoked as fn(uint32_t id, const T& value)
     */
    template <typename F> void forEach(F &&fn) const
    {
        for (uint32_t i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].live) {
                fn(i, m_slots[i].value);
            }
        }
    }

  private:
    struct Slot {
        T value;
        bool live = false;
    };

    std::vector<Slot> m_slots;
    std::vector<uint32_t> m_freeSlots;
    uint32_t m_liveCount = 0;
};

} // namespace Rapture

#endif // RAPTURE__FREE_LIST_H

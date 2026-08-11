#ifndef RAPTURE__SPARSE_SET_H
#define RAPTURE__SPARSE_SET_H

#include "common.h"

#include <array>
#include <memory>
#include <vector>

namespace Rapture {
namespace ecs {

inline constexpr uint32_t DENSE_INVALID = ~uint32_t(0);
inline constexpr uint32_t SPARSE_PAGE_SIZE = 4096;

using SparsePage = std::array<uint32_t, SPARSE_PAGE_SIZE>;

/**
 * @brief Maps entities to positions in a packed array, and back.
 *
 * The entity to position direction is a paged array, so a pool only pays for the
 * ranges of entity index space it actually holds.
 */
class SparseSet {
  public:
    /**
     * @brief Tests membership.
     * @param entity Entity to look for.
     * @return True if the entity has a position in this set, false for a stale generation.
     */
    bool contains(Entity entity) const;

    /**
     * @brief Position of an entity in the packed array.
     * @param entity Entity to look up, must be contained.
     * @return Dense index.
     */
    uint32_t getDenseIndex(Entity entity) const;

    /**
     * @brief Appends an entity at the end of the packed array.
     * @param entity Entity to add, must not already be contained.
     * @return Dense index the entity was placed at.
     */
    uint32_t insert(Entity entity);

    /**
     * @brief Removes an entity by swapping the last element into its place.
     * @param entity Entity to remove, must be contained.
     * @return Dense index that was vacated, so parallel arrays can mirror the swap.
     */
    uint32_t remove(Entity entity);

    void clear();

    uint32_t getSize() const;

    /**
     * @brief The packed entity array.
     * @return Entities in dense order, ordering depends on insertion and removal history.
     */
    const std::vector<Entity> &getEntities() const;

  private:
    /**
     * @brief Looks up the sparse slot of an entity index without allocating.
     * @param entityIndex Index part of an entity.
     * @return Pointer to the slot, or nullptr if its page does not exist.
     */
    const uint32_t *findSlot(uint32_t entityIndex) const;

    /**
     * @brief Looks up the sparse slot of an entity index, allocating its page if needed.
     * @param entityIndex Index part of an entity.
     * @return Reference to the slot.
     */
    uint32_t &assureSlot(uint32_t entityIndex);

    std::vector<std::unique_ptr<SparsePage>> m_pages;
    std::vector<Entity> m_dense;
};

} // namespace ecs
} // namespace Rapture

#endif // RAPTURE__SPARSE_SET_H

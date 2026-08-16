#ifndef RAPTURE__ECS_COMMON_H
#define RAPTURE__ECS_COMMON_H

#include <cstdint>

namespace Rapture {
namespace ecs {

using Entity = uint32_t;
using ComponentTypeId = uint32_t;
using ComponentMask = uint64_t;

inline constexpr uint32_t COMPONENT_TYPE_MAX = 64;
inline constexpr uint32_t ENTITY_FREE_LIST_END = ~uint32_t(0);
inline constexpr uint16_t ENTITY_FLAG_ALIVE = 1 << 0;

inline constexpr uint32_t ENTITY_INDEX_BITS = 20;
inline constexpr uint32_t ENTITY_GENERATION_BITS = 12;
inline constexpr uint32_t ENTITY_INDEX_MASK = (1u << ENTITY_INDEX_BITS) - 1u;
inline constexpr uint32_t ENTITY_GENERATION_MASK = (1u << ENTITY_GENERATION_BITS) - 1u;
inline constexpr uint32_t ENTITY_MAX_COUNT = 1u << ENTITY_INDEX_BITS;
inline constexpr Entity ENTITY_NULL = ~Entity(0);

/**
 * @brief Per entity bookkeeping, one for every slot in the entity index space.
 */
struct EntityRecord {
    ComponentMask components = 0;
    uint32_t nextFree = ENTITY_FREE_LIST_END;
    uint16_t generation = 0;
    uint16_t flags = 0;
};

/**
 * @brief Extracts the slot index of an entity.
 * @param entity Entity to decompose.
 * @return Index into the registry's entity records.
 */
inline constexpr uint32_t EntityIndex(Entity entity)
{
    return entity & ENTITY_INDEX_MASK;
}

/**
 * @brief Extracts the reuse counter of an entity.
 * @param entity Entity to decompose.
 * @return Generation the entity was handed out with.
 */
inline constexpr uint32_t EntityGeneration(Entity entity)
{
    return (entity >> ENTITY_INDEX_BITS) & ENTITY_GENERATION_MASK;
}

/**
 * @brief Composes an entity from a slot index and a generation.
 * @param index Slot index, must fit in ENTITY_INDEX_BITS.
 * @param generation Reuse counter, wraps at ENTITY_GENERATION_BITS.
 * @return The packed entity.
 */
inline constexpr Entity MakeEntity(uint32_t index, uint32_t generation)
{
    return (index & ENTITY_INDEX_MASK) | ((generation & ENTITY_GENERATION_MASK) << ENTITY_INDEX_BITS);
}

} // namespace ecs
} // namespace Rapture

#endif // RAPTURE__ECS_COMMON_H

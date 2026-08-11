#include "registry.h"

#include <bit>

namespace Rapture {
namespace ecs {

Entity Registry::create()
{
    uint32_t index;

    if (m_freeHead != ENTITY_FREE_LIST_END) {
        index = m_freeHead;
        m_freeHead = m_records[index].nextFree;
    } else {
        RP_ASSERT(m_records.size() < ENTITY_MAX_COUNT, "entity index space exhausted");
        index = static_cast<uint32_t>(m_records.size());
        m_records.emplace_back();
        m_journal.growTo(index + 1);
    }

    EntityRecord &record = m_records[index];
    record.components = 0;
    record.nextFree = ENTITY_FREE_LIST_END;
    record.flags |= ENTITY_FLAG_ALIVE;
    m_aliveCount++;

    return MakeEntity(index, record.generation);
}

void Registry::destroy(Entity entity)
{
    RP_ASSERT(isValid(entity), "cannot destroy a dead entity");

    uint32_t index = EntityIndex(entity);
    EntityRecord &record = m_records[index];

    ComponentMask attached = record.components;

    ComponentMask remaining = attached;
    while (remaining != 0) {
        uint32_t typeId = static_cast<uint32_t>(std::countr_zero(remaining));
        remaining &= remaining - 1;
        m_pools[typeId]->getDestroySignal().fire(entity);
    }

    remaining = attached;
    while (remaining != 0) {
        uint32_t typeId = static_cast<uint32_t>(std::countr_zero(remaining));
        remaining &= remaining - 1;
        m_pools[typeId]->remove(entity);
    }

    record.components = 0;
    record.generation = (record.generation + 1) & ENTITY_GENERATION_MASK;
    record.flags &= ~ENTITY_FLAG_ALIVE;
    record.nextFree = m_freeHead;
    m_freeHead = index;
    m_aliveCount--;
}

bool Registry::isValid(Entity entity) const
{
    uint32_t index = EntityIndex(entity);
    if (index >= m_records.size()) {
        return false;
    }

    const EntityRecord &record = m_records[index];
    if ((record.flags & ENTITY_FLAG_ALIVE) == 0) {
        return false;
    }
    return record.generation == EntityGeneration(entity);
}

void Registry::clear()
{
    for (auto &pool : m_pools) {
        if (pool != nullptr) {
            pool->clear();
        }
    }

    m_records.clear();
    m_freeHead = ENTITY_FREE_LIST_END;
    m_aliveCount = 0;
}

uint32_t Registry::getAliveCount() const
{
    return m_aliveCount;
}

ComponentMask Registry::getComponentMask(Entity entity) const
{
    RP_ASSERT(isValid(entity), "cannot inspect a dead entity");
    return m_records[EntityIndex(entity)].components;
}

const std::vector<EntityRecord> &Registry::getRecords() const
{
    return m_records;
}

} // namespace ecs
} // namespace Rapture

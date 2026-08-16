#include "sparse_set.h"

#include "core/utils/rp_assert.h"

namespace Rapture {
namespace ecs {

const uint32_t *SparseSet::findSlot(uint32_t entityIndex) const
{
    uint32_t page = entityIndex / SPARSE_PAGE_SIZE;
    if (page >= m_pages.size() || m_pages[page] == nullptr) {
        return nullptr;
    }
    return &(*m_pages[page])[entityIndex % SPARSE_PAGE_SIZE];
}

uint32_t &SparseSet::assureSlot(uint32_t entityIndex)
{
    uint32_t page = entityIndex / SPARSE_PAGE_SIZE;
    if (page >= m_pages.size()) {
        m_pages.resize(page + 1);
    }
    if (m_pages[page] == nullptr) {
        m_pages[page] = std::make_unique<SparsePage>();
        m_pages[page]->fill(DENSE_INVALID);
    }
    return (*m_pages[page])[entityIndex % SPARSE_PAGE_SIZE];
}

bool SparseSet::contains(Entity entity) const
{
    const uint32_t *slot = findSlot(EntityIndex(entity));
    if (slot == nullptr || *slot == DENSE_INVALID) {
        return false;
    }
    return m_dense[*slot] == entity;
}

uint32_t SparseSet::getDenseIndex(Entity entity) const
{
    const uint32_t *slot = findSlot(EntityIndex(entity));
    RP_ASSERT(slot != nullptr && *slot != DENSE_INVALID, "entity is not in this set");
    return *slot;
}

uint32_t SparseSet::insert(Entity entity)
{
    uint32_t &slot = assureSlot(EntityIndex(entity));
    RP_ASSERT(slot == DENSE_INVALID, "entity is already in this set");

    slot = static_cast<uint32_t>(m_dense.size());
    m_dense.push_back(entity);
    return slot;
}

uint32_t SparseSet::remove(Entity entity)
{
    uint32_t &slot = assureSlot(EntityIndex(entity));
    RP_ASSERT(slot != DENSE_INVALID, "entity is not in this set");

    uint32_t vacated = slot;
    uint32_t last = static_cast<uint32_t>(m_dense.size()) - 1;

    if (vacated != last) {
        Entity moved = m_dense[last];
        m_dense[vacated] = moved;
        assureSlot(EntityIndex(moved)) = vacated;
    }

    slot = DENSE_INVALID;
    m_dense.pop_back();
    return vacated;
}

void SparseSet::clear()
{
    m_pages.clear();
    m_dense.clear();
}

uint32_t SparseSet::getSize() const
{
    return static_cast<uint32_t>(m_dense.size());
}

const std::vector<Entity> &SparseSet::getEntities() const
{
    return m_dense;
}

} // namespace ecs
} // namespace Rapture

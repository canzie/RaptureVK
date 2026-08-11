#include "EntitySelection.h"

void EntitySelection::select(Rapture::ecs::EntityAccessor entity)
{
    if (m_entity == entity) {
        return;
    }

    m_entity = entity;
    onChanged.fire(m_entity);
}

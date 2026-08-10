#include "EntitySelection.h"

void EntitySelection::select(Rapture::Entity entity)
{
    if (m_entity == entity) {
        return;
    }

    m_entity = entity;
    onChanged.fire(m_entity);
}

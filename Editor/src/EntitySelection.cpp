#include "EntitySelection.h"

void EntitySelection::select(Rapture::ecs::EntityAccessor entity)
{
    if (m_onPicked) {
        // taken before the call, so a pick that arms another one leaves that one standing
        auto onPicked = std::move(m_onPicked);
        m_onPicked = nullptr;
        onPickingChanged.fire(false);

        onPicked(entity);
        return;
    }

    if (m_entity == entity) {
        return;
    }

    m_entity = entity;
    onChanged.fire(m_entity);
}

void EntitySelection::requestPick(std::function<void(Rapture::ecs::EntityAccessor)> onPicked)
{
    m_onPicked = std::move(onPicked);
    onPickingChanged.fire(isPicking());
}

void EntitySelection::cancelPick(void)
{
    if (!m_onPicked) {
        return;
    }

    m_onPicked = nullptr;
    onPickingChanged.fire(false);
}

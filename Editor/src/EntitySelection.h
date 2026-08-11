#ifndef RAPTURE__ENTITY_SELECTION_H
#define RAPTURE__ENTITY_SELECTION_H

#include "events/EventSignal.h"
#include "ecs/entity_accessor.h"

/**
 * @brief The entity a workspace has selected.
 *
 * Selection is scoped to the workspace it is owned by, so what is picked in one never reaches the
 * panels of another.
 */
class EntitySelection {
  public:
    /**
     * @brief Selects an entity, firing onChanged when it differs from the current one
     * @param entity The entity to select, invalid to select nothing
     */
    void select(Rapture::ecs::EntityAccessor entity);

    /**
     * @brief Selects nothing
     */
    void clear(void) { select(Rapture::ecs::EntityAccessor()); }

    Rapture::ecs::EntityAccessor entity() const { return m_entity; }
    bool has() const { return m_entity.isValid(); }

  public:
    /**
     * @brief Fires with the newly selected entity, which is invalid when the selection was cleared
     */
    Rapture::EventSignal<void(Rapture::ecs::EntityAccessor)> onChanged;

  private:
    Rapture::ecs::EntityAccessor m_entity;
};

#endif // RAPTURE__ENTITY_SELECTION_H

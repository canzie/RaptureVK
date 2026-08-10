#ifndef RAPTURE__ENTITY_SELECTION_H
#define RAPTURE__ENTITY_SELECTION_H

#include "events/EventSignal.h"
#include "scenes/entities/Entity.h"

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
    void select(Rapture::Entity entity);

    /**
     * @brief Selects nothing
     */
    void clear(void) { select(Rapture::Entity()); }

    Rapture::Entity entity() const { return m_entity; }
    bool has() const { return m_entity.isValid(); }

  public:
    /**
     * @brief Fires with the newly selected entity, which is invalid when the selection was cleared
     */
    Rapture::EventSignal<void(Rapture::Entity)> onChanged;

  private:
    Rapture::Entity m_entity;
};

#endif // RAPTURE__ENTITY_SELECTION_H

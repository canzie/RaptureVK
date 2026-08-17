#ifndef RAPTURE__ENTITY_SELECTION_H
#define RAPTURE__ENTITY_SELECTION_H

#include "core/events/EventSignal.h"
#include "core/ecs/entity_accessor.h"

#include <functional>

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

    /**
     * @brief Hands the next entity selected to a callback instead of selecting it
     * @param onPicked Called with that entity, after which the selection goes back to selecting
     */
    void requestPick(std::function<void(Rapture::ecs::EntityAccessor)> onPicked);

    /**
     * @brief Drops a pick request, leaving the selection selecting again
     */
    void cancelPick(void);

    bool isPicking() const { return static_cast<bool>(m_onPicked); }

  public:
    /**
     * @brief Fires with the newly selected entity, which is invalid when the selection was cleared
     */
    Rapture::EventSignal<void(Rapture::ecs::EntityAccessor)> onChanged;

    /**
     * @brief Fires when a pick request is taken or dropped, with whether one is now pending
     */
    Rapture::EventSignal<void(bool)> onPickingChanged;

  private:
    Rapture::ecs::EntityAccessor m_entity;
    std::function<void(Rapture::ecs::EntityAccessor)> m_onPicked;
};

#endif // RAPTURE__ENTITY_SELECTION_H

#ifndef RAPTURE__COMPONENT_EDITOR_BASE_H
#define RAPTURE__COMPONENT_EDITOR_BASE_H

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include "layers/panels/components/property_sections.h"
#include "core/ecs/entity_accessor.h"
#include "scene/Scene.h"
#include "scene/EntityCommon.h"

/**
 * @brief A property section that edits one component of the selected entity.
 */
class ComponentEditorBase : public PropertySection {
  public:
    void sync() override
    {
        sync(m_entity);
        m_subjectChanged = false;
    }

    /**
     * @brief Pushes the entity's component data into the bound widget buffers.
     * @param entity The entity this section edits.
     */
    virtual void sync(const Rapture::ecs::EntityAccessor &entity) = 0;

    /**
     * @brief Points this editor at what it edits, before its body is built.
     * @param scene The scene the entity lives in.
     * @param entity The entity this editor edits.
     */
    virtual void setSubject(Rapture::Scene *scene, const Rapture::ecs::EntityAccessor &entity)
    {
        m_subjectChanged = m_subjectChanged || !(entity == m_entity);
        m_scene = scene;
        m_entity = entity;
    }

  protected:
    /**
     * @brief Adds the mobility row every movable node shares.
     * @param t Scope of the table the row is added to.
     * @param current Mobility the dropdown opens on.
     * @param onSelect Called with the picked mobility.
     * @return The dropdown, so its text can be updated as the entity changes.
     */
    Amethyst::Dropdown *rowMobility(Amethyst::TableScope &t, Rapture::Mobility current,
                                    const std::function<void(Rapture::Mobility)> &onSelect);

    /**
     * @brief Whether this editor was pointed at a different entity since it last synced.
     * @return True while the widgets still hold the previous entity's values.
     */
    bool subjectChanged() const { return m_subjectChanged; }

  protected:
    Rapture::ecs::EntityAccessor m_entity;
    Rapture::Scene *m_scene = nullptr;
    bool m_subjectChanged = true;
};

#endif // RAPTURE__COMPONENT_EDITOR_BASE_H

#ifndef RAPTURE__COMPONENT_EDITOR_BASE_H
#define RAPTURE__COMPONENT_EDITOR_BASE_H

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include "layers/panels/components/property_sections.h"
#include "scenes/entities/Entity.h"
#include "scenes/entities/EntityCommon.h"

/**
 * @brief A property section that edits one component of the selected entity.
 */
class ComponentEditorBase : public PropertySection {
  public:
    void sync() override { sync(entity); }

    /**
     * @brief Pushes the entity's component data into the bound widget buffers.
     * @param entity The entity this section edits.
     */
    virtual void sync(const Rapture::Entity &entity) = 0;

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

  public:
    Rapture::Entity entity;
};

#endif // RAPTURE__COMPONENT_EDITOR_BASE_H

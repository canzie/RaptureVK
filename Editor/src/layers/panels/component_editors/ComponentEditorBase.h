#ifndef RAPTURE__COMPONENT_EDITOR_BASE_H
#define RAPTURE__COMPONENT_EDITOR_BASE_H

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include "scenes/entities/Entity.h"

/**
 * @brief A single collapsible section in the Properties panel that edits one component.
 *
 * Each editor owns its own widgets and scratch value buffers. The panel creates one per
 * component type the selected entity has, reuses it while that component stays present, and
 * destroys it when the component is gone.
 */
class ComponentEditorBase {
  public:
    virtual ~ComponentEditorBase() = default;

    /**
     * @brief Section title shown in the collapsible header.
     */
    virtual const char *title() const = 0;

    /**
     * @brief SVG icon shown next to the title, or empty for none.
     */
    virtual const char *icon() const = 0;

    /**
     * @brief Current expanded body height in pixels, may vary with state.
     */
    virtual float bodyHeight() const { return m_bodyHeight; }

    /**
     * @brief Builds the widgets into the section body once, on creation.
     */
    virtual void buildBody(Amethyst::CollapsibleHeaderScope &ch) = 0;

    /**
     * @brief Pushes the entity's component data into the bound widget buffers.
     */
    virtual void sync(const Rapture::Entity &entity) = 0;

    Amethyst::CollapsibleHeader *header = nullptr;

  protected:
    float m_bodyHeight = 0.0f;
};

#endif // RAPTURE__COMPONENT_EDITOR_BASE_H

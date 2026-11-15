#ifndef RAPTURE__PROPERTIES_PANEL_H
#define RAPTURE__PROPERTIES_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include <rapture/core/events/EventSignal.h>
#include "inspector/ComponentEditorBase.h"
#include "inspector/PropertySections.h"
#include "panels/Panel.h"
#include <rapture/scene/Scene.h>
#include <rapture/core/ecs/entity_accessor.h>

#include <concepts>
#include <optional>

class PropertiesPanel : public Panel {
  public:
    explicit PropertiesPanel(const WorkspaceContext &context);

    void setScene(Rapture::Scene *scene);
    void onUpdate(float dt) override;

  protected:
    void build(Amethyst::UIScope &scope) override;
    void onRootDestroyed() override;

  private:
    void setupSearchBar(Amethyst::UIScope &scope);
    void setupPlaceholder(Amethyst::UIScope &scope);
    void setupEntityView();

    /**
     * @brief Ensures the section for T and points it at the selected entity.
     * @tparam T A ComponentEditorBase-derived editor type.
     * @param present Whether the selected entity has the component this editor edits.
     */
    template <std::derived_from<ComponentEditorBase> T>
    void ensure(bool present)
    {
        if (T *editor = m_sections->ensure<T>(present)) {
            editor->setSelection(m_selection);
            editor->setSubject(m_scene, m_selectedEntity);
        }
    }

    void refresh(void);
    void showEntity(const Rapture::ecs::EntityAccessor &entity);
    void showPlaceholder(void);
    void clearSelection(void);

  private:
    Amethyst::TextLabel *m_placeholderText = nullptr;
    Amethyst::TextInput *m_searchInput = nullptr;
    std::optional<PropertySectionList> m_sections;

    Rapture::Scene *m_scene = nullptr;
    Rapture::ecs::EntityAccessor m_selectedEntity;
    Rapture::EventConnection m_selectionChangedConn;
};

#endif // RAPTURE__PROPERTIES_PANEL_H

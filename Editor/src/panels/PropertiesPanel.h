#ifndef RAPTURE__PROPERTIES_PANEL_H
#define RAPTURE__PROPERTIES_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include "inspector/ComponentEditorBase.h"
#include "inspector/PropertySections.h"
#include "panels/Panel.h"
#include <rapture/core/ecs/entity_accessor.h>
#include <rapture/core/events/EventSignal.h>
#include <rapture/scene/Scene.h>

#include <concepts>
#include <optional>

class IconLabelButton;
class SearchField;

class PropertiesPanel : public Panel {
  public:
    std::string_view typeName() const override { return "PropertiesPanel"; }
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
        m_sections->ensurePrepared<T>(present, [this](T &editor) {
            editor.setSelection(m_selection);
            editor.setSubject(m_scene, m_selectedEntity);
        });
    }

    void refresh(void);
    void showEntity(const Rapture::ecs::EntityAccessor &entity);
    void showPlaceholder(void);
    void clearSelection(void);

  private:
    Amethyst::TextLabel *m_placeholderText = nullptr;
    SearchField *m_searchField = nullptr;
    IconLabelButton *m_addComponentButton = nullptr;
    std::optional<PropertySectionList> m_sections;

    Rapture::Scene *m_scene = nullptr;
    Rapture::ecs::EntityAccessor m_selectedEntity;
    Rapture::EventConnection m_selectionChangedConn;
};

#endif // RAPTURE__PROPERTIES_PANEL_H

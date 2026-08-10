#ifndef RAPTURE__PROPERTIES_PANEL_H
#define RAPTURE__PROPERTIES_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include "events/EventSignal.h"
#include "layers/panels/Panel.h"
#include "layers/panels/component_editors/ComponentEditorBase.h"
#include "layers/panels/components/property_sections.h"
#include "scenes/Scene.h"
#include "scenes/entities/Entity.h"

#include <concepts>
#include <optional>

class PropertiesPanel : public Panel {
  public:
    PropertiesPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context);
    ~PropertiesPanel();
    PropertiesPanel(const PropertiesPanel &) = delete;
    PropertiesPanel &operator=(const PropertiesPanel &) = delete;
    PropertiesPanel(PropertiesPanel &&) = delete;
    PropertiesPanel &operator=(PropertiesPanel &&) = delete;

    void setScene(Rapture::Scene *scene);
    void onUpdate(float dt) override;

  private:
    void setupSearchBar(void);
    void setupPlaceholder(void);
    void setupEntityView(void);

    /**
     * @brief Ensures the section for T and points it at the selected entity.
     * @tparam T A ComponentEditorBase-derived editor type.
     * @param present Whether the selected entity has the component this editor edits.
     */
    template <std::derived_from<ComponentEditorBase> T>
    void ensure(bool present)
    {
        if (T *editor = m_sections->ensure<T>(present)) {
            editor->entity = m_selectedEntity;
        }
    }

    void refresh(void);
    void showEntity(const Rapture::Entity &entity);
    void showPlaceholder(void);
    void clearSelection(void);

  private:
    Amethyst::TextLabel *m_placeholderText = nullptr;
    Amethyst::TextInput *m_searchInput = nullptr;
    std::optional<PropertySectionList> m_sections;

    Rapture::Scene *m_scene = nullptr;
    Rapture::Entity m_selectedEntity;
    Rapture::EventConnection m_selectionChangedConn;
};

#endif // RAPTURE__PROPERTIES_PANEL_H

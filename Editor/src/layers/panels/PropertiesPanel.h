#ifndef RAPTURE__PROPERTIES_PANEL_H
#define RAPTURE__PROPERTIES_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include "layers/panels/Panel.h"
#include "layers/panels/component_editors/ComponentEditorBase.h"
#include "scenes/Scene.h"
#include "scenes/entities/Entity.h"

#include <concepts>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

class PropertiesPanel : public Panel {
  public:
    PropertiesPanel(Amethyst::TabBar *tabBar);
    ~PropertiesPanel();
    PropertiesPanel(const PropertiesPanel &) = delete;
    PropertiesPanel &operator=(const PropertiesPanel &) = delete;
    PropertiesPanel(PropertiesPanel &&) = delete;
    PropertiesPanel &operator=(PropertiesPanel &&) = delete;

    void setScene(std::shared_ptr<Rapture::Scene> scene);
    void onUpdate(float dt) override;

  private:
    void setupSearchBar();
    void setupPlaceholder();
    void setupEntityView();

    /**
     * @brief Creates the editor for T if its component is present and missing, reuses it if it
     * already exists, or destroys it if present is false. Active editors are collected for layout.
     * @tparam T A ComponentEditorBase-derived editor type.
     * @param present Whether the selected entity has the component this editor edits.
     */
    template <std::derived_from<ComponentEditorBase> T>
    void ensure(bool present)
    {
        std::type_index key(typeid(T));
        auto it = m_editors.find(key);

        if (present) {
            if (it == m_editors.end()) {
                auto editor = std::make_unique<T>();
                buildSection(*editor);
                it = m_editors.emplace(key, std::move(editor)).first;
            }
            m_active.push_back(it->second.get());
        } else if (it != m_editors.end()) {
            if (it->second->header != nullptr) {
                m_entityView->removeChild(it->second->header);
            }
            m_editors.erase(it);
        }
    }

    /**
     * @brief Builds an editor's collapsible header under the entity view and fills its body.
     */
    void buildSection(ComponentEditorBase &editor);

    void refresh();
    void relayout();
    void showEntity(const Rapture::Entity &entity);
    void showPlaceholder();

    Amethyst::TabBar *m_hostTabBar = nullptr;
    Amethyst::Frame *m_root = nullptr;
    Amethyst::TextLabel *m_placeholderText = nullptr;
    Amethyst::ScrollingFrame *m_entityView = nullptr;
    Amethyst::TextInput *m_searchInput = nullptr;

    std::unordered_map<std::type_index, std::unique_ptr<ComponentEditorBase>> m_editors;
    std::vector<ComponentEditorBase *> m_active;

    std::shared_ptr<Rapture::Scene> m_scene;
    Rapture::Entity m_selectedEntity;
    size_t m_entitySelectedListenerID = 0;
};

#endif // RAPTURE__PROPERTIES_PANEL_H

#ifndef RAPTURE__PROPERTIES_PANEL_H
#define RAPTURE__PROPERTIES_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/table.h>

#include "layers/panels/Panel.h"
#include "scenes/Scene.h"
#include "scenes/entities/Entity.h"
#include <memory>

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
    void showEntity(const Rapture::Entity &entity);
    void showPlaceholder();

  private:
    Amethyst::TabBar *m_hostTabBar = nullptr;
    Amethyst::Frame *m_root = nullptr;
    Amethyst::TextLabel *m_placeholderText = nullptr;
    Amethyst::ScrollingFrame *m_entityView = nullptr;
    Amethyst::TextInput *m_searchInput = nullptr;
    Amethyst::CollapsibleHeader *m_transformHeader = nullptr;
    Amethyst::Table *m_transformTable = nullptr;
    Amethyst::SliderVec3 *m_transformSliders[3] = {};
    Amethyst::vec3 m_transformValues[3] = {};

    std::shared_ptr<Rapture::Scene> m_scene;
    Rapture::Entity m_selectedEntity;
    size_t m_entitySelectedListenerID = 0;
};

#endif // RAPTURE__PROPERTIES_PANEL_H

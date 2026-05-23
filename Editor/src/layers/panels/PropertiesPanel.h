#ifndef RAPTURE__PROPERTIES_PANEL_H
#define RAPTURE__PROPERTIES_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/common.h>
#include <components/docking_layer.h>
#include <components/frame.h>
#include <components/panel_layer.h>
#include <components/slider.h>
#include <components/table.h>
#include <components/text_label.h>

#include "layers/panels/Panel.h"
#include "scenes/Scene.h"
#include "scenes/entities/Entity.h"
#include <memory>

class PropertiesPanel : public Panel {
  public:
    PropertiesPanel(Amethyst::DockingLayer *dockingLayer);
    ~PropertiesPanel();
    PropertiesPanel(const PropertiesPanel &) = delete;
    PropertiesPanel &operator=(const PropertiesPanel &) = delete;
    PropertiesPanel(PropertiesPanel &&) = delete;
    PropertiesPanel &operator=(PropertiesPanel &&) = delete;

    void setScene(std::shared_ptr<Rapture::Scene> scene);

    void onUpdate(float dt) override;

  private:
    void setupPlaceholder();
    void setupEntityView();
    void showEntity(const Rapture::Entity &entity);
    void showPlaceholder();

  private:
    Amethyst::DockingLayer *m_dockingLayer = nullptr;
    Amethyst::PanelLayer *m_root = nullptr;
    Amethyst::TextLabel *m_placeholderText = nullptr;
    Amethyst::ScrollingFrame *m_entityView = nullptr;
    Amethyst::CollapsibleHeader *m_transformHeader = nullptr;
    Amethyst::Table *m_transformTable = nullptr;
    Amethyst::SliderVec3 *m_transformSliders[3] = {};
    glm::vec3 m_transformValues[3] = {};

    std::shared_ptr<Rapture::Scene> m_scene;
    Rapture::Entity m_selectedEntity;
    size_t m_entitySelectedListenerID = 0;
};

#endif // RAPTURE__PROPERTIES_PANEL_H

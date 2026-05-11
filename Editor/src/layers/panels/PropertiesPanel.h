#ifndef RAPTURE__PROPERTIES_PANEL_H
#define RAPTURE__PROPERTIES_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/common.h>
#include <components/docking_layer.h>
#include <components/frame.h>
#include <components/panel_layer.h>
#include <components/text_label.h>

class PropertiesPanel {
  public:
    PropertiesPanel(Amethyst::DockingLayer *dockingLayer);
    ~PropertiesPanel();
    PropertiesPanel(const PropertiesPanel &) = delete;
    PropertiesPanel &operator=(const PropertiesPanel &) = delete;
    PropertiesPanel(PropertiesPanel &&) = delete;
    PropertiesPanel &operator=(PropertiesPanel &&) = delete;

  private:
    Amethyst::DockingLayer *m_dockingLayer = nullptr;
    Amethyst::PanelLayer *m_root = nullptr;
    Amethyst::TextLabel *m_placeholderText = nullptr;
};

#endif // RAPTURE__PROPERTIES_PANEL_H

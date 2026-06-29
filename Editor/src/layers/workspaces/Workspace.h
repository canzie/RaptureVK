#ifndef RAPTURE__WORKSPACE_H
#define RAPTURE__WORKSPACE_H

#include "layers/panels/Panel.h"
#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>
#include <memory>
#include <string_view>
#include <vector>

class Workspace {
  public:
    virtual ~Workspace() = default;

    Amethyst::DockingLayer *getDockingLayer() const { return m_dockingLayer; }
    const std::vector<std::unique_ptr<Panel>> &getPanels() const { return m_panels; }

    virtual void onUpdate(float dt);
    virtual void saveLayout() = 0;

  protected:
    void setupBase(Amethyst::TabBarScope &tabs, std::string_view label);

  public:
    bool active = false;
    bool focused = false;

  protected:
    Amethyst::Frame *m_container = nullptr;
    Amethyst::Frame *m_hotbar = nullptr;
    Amethyst::DockingLayer *m_dockingLayer = nullptr;
    std::vector<std::unique_ptr<Panel>> m_panels;
};

#endif // RAPTURE__WORKSPACE_H

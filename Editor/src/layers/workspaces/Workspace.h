#ifndef RAPTURE__WORKSPACE_H
#define RAPTURE__WORKSPACE_H

#include "layers/panels/Panel.h"
#include "layers/panels/common.h"
#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>
#include <memory>
#include <string_view>
#include <vector>

class Workspace {
  public:
    virtual ~Workspace();

    Amethyst::DockingLayer *getDockingLayer(void) const { return m_dockingLayer; }
    const std::vector<std::unique_ptr<Panel>> &getPanels(void) const { return m_panels; }
    const WorkspaceContext &getContext(void) const { return m_context; }

    /**
     * @brief Dock a panel into this workspace's layout.
     * @param panel The panel to add.
     * @param zone Side of the layout the panel is docked on.
     */
    void addPanel(std::unique_ptr<Panel> panel, Amethyst::DockZone zone);

    virtual void onUpdate(float dt);
    virtual void saveLayout(void) = 0;

  protected:
    void setupBase(Amethyst::TabBarScope &tabs, std::string_view label);

  public:
    bool active = false;
    bool focused = false;

  protected:
    WorkspaceContext m_context;
    Amethyst::Frame *m_container = nullptr;
    Amethyst::Frame *m_hotbar = nullptr;
    Amethyst::DockingLayer *m_dockingLayer = nullptr;
    std::vector<std::unique_ptr<Panel>> m_panels;
    bool m_teardown = false;
};

#endif // RAPTURE__WORKSPACE_H

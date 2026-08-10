#ifndef RAPTURE__WORKSPACE_H
#define RAPTURE__WORKSPACE_H

#include "EntitySelection.h"
#include "layers/panels/Panel.h"
#include "layers/panels/common.h"
#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>
#include <memory>
#include <string_view>
#include <vector>

class Workspace {
  public:
    Workspace() { m_context.selection = &m_selection; }
    virtual ~Workspace();

    Amethyst::DockingLayer *getDockingLayer(void) const { return m_dockingLayer; }
    const std::vector<std::unique_ptr<Panel>> &getPanels(void) const { return m_panels; }
    const WorkspaceContext &getContext(void) const { return m_context; }

    /**
     * @brief The frame this workspace fills, which is the content of the tab it lives in.
     */
    Amethyst::Frame *getContainer(void) const { return m_container; }

    /**
     * @brief Dock a panel into this workspace's layout.
     * @param panel The panel to add.
     * @param zone Side of the layout the panel is docked on.
     */
    void addPanel(std::unique_ptr<Panel> panel, Amethyst::DockZone zone);

    virtual void onUpdate(float dt);
    virtual void saveLayout(void) = 0;

  protected:
    /**
     * @brief Builds this workspace into a tab declared while its bar is being built.
     * @param tabs Scope of the tab bar this workspace's tab is declared in.
     * @param label Text shown on the tab.
     */
    void setupBase(Amethyst::TabBarScope &tabs, std::string_view label);

    /**
     * @brief Builds this workspace into a tab appended to a bar that is already up.
     * @param tabBar The bar to append the tab to.
     * @param label Text shown on the tab.
     * @param iconSvg SVG markup shown to the left of the label, empty to omit it.
     */
    void setupBase(Amethyst::TabBar &tabBar, std::string_view label, std::string_view iconSvg = {});

  private:
    /**
     * @brief Fills a tab's content frame with the hotbar and docking layer every workspace has.
     * @param container The tab content frame this workspace fills.
     */
    void buildContainer(Amethyst::Frame &container);

  public:
    bool active = false;
    bool focused = false;

  protected:
    EntitySelection m_selection;
    WorkspaceContext m_context;
    Amethyst::Frame *m_container = nullptr;
    Amethyst::Frame *m_hotbar = nullptr;
    Amethyst::DockingLayer *m_dockingLayer = nullptr;
    std::vector<std::unique_ptr<Panel>> m_panels;
    bool m_teardown = false;
};

#endif // RAPTURE__WORKSPACE_H

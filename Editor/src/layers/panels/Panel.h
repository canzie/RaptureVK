#ifndef RAPTURE__PANEL_H
#define RAPTURE__PANEL_H

#include "layers/panels/common.h"
#include "layers/panels/components/tab_layouts.h"
#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

class Panel {
  public:
    explicit Panel(std::string name, WorkspaceContext context)
        : m_name(name), m_selection(context.selection), m_services(std::move(context.services))
    {
    }
    explicit Panel(std::string name, PanelServices services) : m_name(name), m_services(std::move(services)) {}
    virtual ~Panel() = default;
    void attach(Amethyst::TabBar *tabBar, std::unique_ptr<Amethyst::Frame> root)
    {
        root->name = m_name;
        tabBar->addTab(std::move(root), iconTabLayout(m_name, icon));
    }

    void attach(Amethyst::TabBar *tabBar)
    {
        if (m_root == nullptr || m_root->parent == nullptr) {
            return;
        }
        if (m_root->parent->as<Amethyst::TabBar>() != nullptr) {
            return;
        }
        m_root->name = m_name;
        tabBar->addTab(std::move(m_root->parent->removeChild(m_root)), iconTabLayout(m_name, icon));
    }
    virtual void onUpdate(float dt) {}
    virtual void setContext(const WorkspaceContext &context)
    {
        m_selection = context.selection;
        m_services = context.services;
    }
    Amethyst::Frame *root() { return m_root; }

  public:
    std::string_view icon;

  protected:
    std::string m_name;

    Amethyst::Frame *m_root = nullptr;
    Amethyst::EventConnection m_rootDestroyConn;
    EntitySelection *m_selection = nullptr;
    PanelServices m_services;
};

#endif // RAPTURE__PANEL_H

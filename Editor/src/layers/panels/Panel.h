#ifndef RAPTURE__PANEL_H
#define RAPTURE__PANEL_H

#include "layers/panels/common.h"
#include <amethyst/Amethyst.h>

class Panel {
  public:
    explicit Panel(WorkspaceContext context) : m_services(std::move(context.services)) {}
    virtual ~Panel() = default;
    virtual void onUpdate(float dt) {}

  protected:
    Amethyst::EventConnection m_rootDestroyConn;
    PanelServices m_services;
};

#endif // RAPTURE__PANEL_H

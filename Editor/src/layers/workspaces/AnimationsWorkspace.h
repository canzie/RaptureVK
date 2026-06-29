#ifndef RAPTURE__ANIMATIONS_WORKSPACE_H
#define RAPTURE__ANIMATIONS_WORKSPACE_H

#include "Workspace.h"

class AnimationsWorkspace : public Workspace {
  public:
    AnimationsWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services) { m_services = services; setupBase(tabs, "Animations"); }

    void saveLayout() override {}
};

#endif // RAPTURE__ANIMATIONS_WORKSPACE_H

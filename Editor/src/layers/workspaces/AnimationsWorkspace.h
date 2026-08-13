#ifndef RAPTURE__ANIMATIONS_WORKSPACE_H
#define RAPTURE__ANIMATIONS_WORKSPACE_H

#include "Workspace.h"

class AnimationsWorkspace : public Workspace {
  public:
    AnimationsWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services) : Workspace(staticKind())
    {
        m_context.services = services;
        setupBase(tabBar, "Animations", {});
    }

    void saveLayout() override {}

    static constexpr std::string_view staticKind() { return "animations"; }
};

#endif // RAPTURE__ANIMATIONS_WORKSPACE_H

#ifndef RAPTURE__SCRIPTING_WORKSPACE_H
#define RAPTURE__SCRIPTING_WORKSPACE_H

#include "Workspace.h"

class ScriptingWorkspace : public Workspace {
  public:
    ScriptingWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services) : Workspace(staticKind())
    {
        m_context.services = services;
        setupBase(tabBar, "Scripting", {});
    }

    void saveLayout() override {}

    static constexpr std::string_view staticKind() { return "scripting"; }
};

#endif // RAPTURE__SCRIPTING_WORKSPACE_H

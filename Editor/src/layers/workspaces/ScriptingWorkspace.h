#ifndef RAPTURE__SCRIPTING_WORKSPACE_H
#define RAPTURE__SCRIPTING_WORKSPACE_H

#include "Workspace.h"

class ScriptingWorkspace : public Workspace {
  public:
    ScriptingWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services) { m_context.services = services; setupBase(tabs, "Scripting"); }

    void saveLayout() override {}
};

#endif // RAPTURE__SCRIPTING_WORKSPACE_H

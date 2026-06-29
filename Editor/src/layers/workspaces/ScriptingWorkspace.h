#ifndef RAPTURE__SCRIPTING_WORKSPACE_H
#define RAPTURE__SCRIPTING_WORKSPACE_H

#include "Workspace.h"

class ScriptingWorkspace : public Workspace {
  public:
    explicit ScriptingWorkspace(Amethyst::TabBarScope &tabs) { setupBase(tabs, "Scripting"); }

    void saveLayout() override {}
};

#endif // RAPTURE__SCRIPTING_WORKSPACE_H

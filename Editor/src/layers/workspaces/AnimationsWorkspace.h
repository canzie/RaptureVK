#ifndef RAPTURE__ANIMATIONS_WORKSPACE_H
#define RAPTURE__ANIMATIONS_WORKSPACE_H

#include "Workspace.h"

class AnimationsWorkspace : public Workspace {
  public:
    explicit AnimationsWorkspace(Amethyst::TabBarScope &tabs) { setupBase(tabs, "Animations"); }

    void saveLayout() override {}
};

#endif // RAPTURE__ANIMATIONS_WORKSPACE_H

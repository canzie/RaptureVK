#ifndef RAPTURE__LEVEL_EDITOR_WORKSPACE_H
#define RAPTURE__LEVEL_EDITOR_WORKSPACE_H

#include "Workspace.h"

class LevelEditorWorkspace : public Workspace {
  public:
    explicit LevelEditorWorkspace(Amethyst::TabBarScope &tabs);

    void saveLayout() override;
};

#endif // RAPTURE__LEVEL_EDITOR_WORKSPACE_H

#ifndef RAPTURE__LEVEL_EDITOR_WORKSPACE_H
#define RAPTURE__LEVEL_EDITOR_WORKSPACE_H

#include "Workspace.h"

class LevelEditorWorkspace : public Workspace {
  public:
    LevelEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services, Rapture::Scene *scene);

    void saveLayout() override;
};

#endif // RAPTURE__LEVEL_EDITOR_WORKSPACE_H

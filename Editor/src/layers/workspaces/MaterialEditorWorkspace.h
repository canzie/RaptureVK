#ifndef RAPTURE__MATERIAL_EDITOR_WORKSPACE_H
#define RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

#include "Workspace.h"

class MaterialEditorWorkspace : public Workspace {
  public:
    explicit MaterialEditorWorkspace(Amethyst::TabBarScope &tabs) { setupBase(tabs, "Material Editor"); }

    void saveLayout() override {}
};

#endif // RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

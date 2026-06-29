#ifndef RAPTURE__MATERIAL_EDITOR_WORKSPACE_H
#define RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

#include "Workspace.h"

class MaterialEditorWorkspace : public Workspace {
  public:
    MaterialEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services) { m_services = services; setupBase(tabs, "Material Editor"); }

    void saveLayout() override {}
};

#endif // RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

#ifndef RAPTURE__MATERIAL_EDITOR_WORKSPACE_H
#define RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

#include "Workspace.h"

class NodeEditorPanel;

class MaterialEditorWorkspace : public Workspace {
  public:
    MaterialEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services);

    void saveLayout() override;

  private:
    void setupHotbar(void);

    NodeEditorPanel *m_nodeEditor = nullptr;
};

#endif // RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

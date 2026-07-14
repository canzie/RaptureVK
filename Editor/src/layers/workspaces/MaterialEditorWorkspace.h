#ifndef RAPTURE__MATERIAL_EDITOR_WORKSPACE_H
#define RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

#include "Workspace.h"

namespace Rapture {
class Scene;
class Viewport;
}

class NodeEditorPanel;

class MaterialEditorWorkspace : public Workspace {
  public:
    MaterialEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services);
    ~MaterialEditorWorkspace() override;

    void saveLayout() override;

  private:
    void setupHotbar(void);
    void setupPreviewScene(void);

    NodeEditorPanel *m_nodeEditor = nullptr;

    Rapture::Scene *m_previewScene = nullptr;
    Rapture::Viewport *m_previewViewport = nullptr;
};

#endif // RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

#ifndef RAPTURE__LEVEL_EDITOR_WORKSPACE_H
#define RAPTURE__LEVEL_EDITOR_WORKSPACE_H

#include "Workspace.h"

class LevelEditorWorkspace : public Workspace {
  public:
    LevelEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services, Rapture::Scene *scene, Rapture::Viewport *viewport);

    void saveLayout() override;

  private:
    void setupHotbar();

    /**
     * @brief Writes the workspace's scene next to the project's content
     *
     * TODO: temporary, becomes a real save command once scenes are assets.
     */
    void saveScene();
};

#endif // RAPTURE__LEVEL_EDITOR_WORKSPACE_H

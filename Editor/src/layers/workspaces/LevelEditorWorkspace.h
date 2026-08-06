#ifndef RAPTURE__LEVEL_EDITOR_WORKSPACE_H
#define RAPTURE__LEVEL_EDITOR_WORKSPACE_H

#include "Workspace.h"

#include <components/context_menu.h>

class LevelEditorWorkspace : public Workspace {
  public:
    LevelEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services, Rapture::Scene *scene, Rapture::Viewport *viewport);

    void saveLayout() override;

  private:
    void setupHotbar();

    /**
     * @brief Opens the add menu under the hotbar button, adding to the root of the scene
     * @param button The button the menu drops from
     */
    void showAddMenu(Amethyst::TextButton &button);

    /**
     * @brief Saves the workspace's scene into its asset and records it as the project's startup scene
     */
    void saveScene();

  private:
    Amethyst::ContextMenu *m_addMenu = nullptr;
};

#endif // RAPTURE__LEVEL_EDITOR_WORKSPACE_H

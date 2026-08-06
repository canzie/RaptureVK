#ifndef RAPTURE__LEVEL_EDITOR_WORKSPACE_H
#define RAPTURE__LEVEL_EDITOR_WORKSPACE_H

#include "Workspace.h"

#include <components/context_menu.h>
#include <memory>

namespace Rapture {
class Layer;
class SceneAsset;
} // namespace Rapture

class LevelEditorWorkspace : public Workspace {
  public:
    LevelEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services, Rapture::Scene *scene,
                         Rapture::Viewport *viewport);

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

    bool isPlaying() const { return m_snapshot != nullptr; }

    /**
     * @brief Hands the scene over to the play layer, keeping a snapshot to come back to
     */
    void startPlay();

    /**
     * @brief Takes the scene back off the play layer and rewinds it to the snapshot
     */
    void stopPlay();

  private:
    Amethyst::ContextMenu *m_addMenu = nullptr;
    Amethyst::TextButton *m_playButton = nullptr;

    std::unique_ptr<Rapture::SceneAsset> m_snapshot;
    Rapture::Layer *m_playLayer = nullptr;
};

#endif // RAPTURE__LEVEL_EDITOR_WORKSPACE_H

#ifndef RAPTURE__LEVEL_EDITOR_WORKSPACE_H
#define RAPTURE__LEVEL_EDITOR_WORKSPACE_H

#include "Workspace.h"

#include <asset_manager/AssetHandle.h>
#include <components/context_menu.h>
#include <memory>
#include <scenes/World.h>
#include <serialization/SerialDocument.h>

namespace Rapture {
class Layer;
} // namespace Rapture

class LevelEditorWorkspace : public Workspace {
  public:
    LevelEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services, Rapture::AssetPtr<Rapture::World> world,
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
     * @brief Writes the workspace's world back into its asset
     */
    void saveWorld();

    bool isPlaying() const { return m_snapshot.rootView().valid(); }

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

    Rapture::AssetPtr<Rapture::World> m_world;
    Rapture::SerialDocument m_snapshot;
    Rapture::Layer *m_playLayer = nullptr;
};

#endif // RAPTURE__LEVEL_EDITOR_WORKSPACE_H

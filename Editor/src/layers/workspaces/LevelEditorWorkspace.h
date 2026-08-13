#ifndef RAPTURE__LEVEL_EDITOR_WORKSPACE_H
#define RAPTURE__LEVEL_EDITOR_WORKSPACE_H

#include "Workspace.h"

#include <asset_manager/AssetHandle.h>
#include <components/context_menu.h>
#include <memory>
#include <scenes/World.h>
#include <viewport/Viewport.h>

namespace Rapture {
class Layer;
} // namespace Rapture

class LevelEditorWorkspace : public Workspace {
  public:
    LevelEditorWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services, Rapture::AssetPtr<Rapture::World> world);
    ~LevelEditorWorkspace() override;

    void saveLayout() override;

    static constexpr std::string_view staticKind() { return "levelEditor"; }

  private:
    /**
     * @brief Creates the viewport this workspace draws its world into
     */
    void setupViewport();

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

    bool isPlaying() const { return m_world->playState() != Rapture::PlayState::STOPPED; }

    /**
     * @brief Hands the world over to the play layer
     */
    void startPlay();

    /**
     * @brief Takes the world back off the play layer
     */
    void stopPlay();

  private:
    Amethyst::ContextMenu *m_addMenu = nullptr;
    Amethyst::TextButton *m_playButton = nullptr;

    Rapture::AssetPtr<Rapture::World> m_world;
    Rapture::Layer *m_playLayer = nullptr;
    Rapture::ViewportContext m_viewport;
};

#endif // RAPTURE__LEVEL_EDITOR_WORKSPACE_H

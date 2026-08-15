#ifndef RAPTURE__LEVEL_EDITOR_WORKSPACE_H
#define RAPTURE__LEVEL_EDITOR_WORKSPACE_H

#include "Workspace.h"

#include <asset_manager/AssetHandle.h>
#include <components/context_menu.h>
#include <events/EventSignal.h>
#include <memory>
#include <scenes/World.h>
#include <viewport/Viewport.h>

class PlayLayer;

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
     * @brief Offers this workspace's commands for as long as the cursor is over it
     */
    void setupShortcuts();

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

    /**
     * @brief Holds the world where it is, or lets it carry on from there
     */
    void togglePause();

    /**
     * @brief Puts the play and pause buttons into the look of the state the world is in
     */
    void syncPlayButtons();

  private:
    Amethyst::ContextMenu *m_addMenu = nullptr;
    Amethyst::ImageButton *m_playButton = nullptr;
    Amethyst::ImageButton *m_pauseButton = nullptr;
    Amethyst::ImageButton *m_stepButton = nullptr;

    Rapture::AssetPtr<Rapture::World> m_world;
    PlayLayer *m_playLayer = nullptr;
    Rapture::EventConnection m_viewportClickedConn;
    Rapture::ViewportContext m_viewport;
};

#endif // RAPTURE__LEVEL_EDITOR_WORKSPACE_H

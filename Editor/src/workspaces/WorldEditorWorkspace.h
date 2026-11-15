#ifndef RAPTURE__WORLD_EDITOR_WORKSPACE_H
#define RAPTURE__WORLD_EDITOR_WORKSPACE_H

#include "AssetWorkspace.h"

#include <components/context_menu.h>
#include <rapture/assets/asset_manager/Asset.h>
#include <rapture/assets/worlds/AWorld.h>
#include <rapture/core/events/EventSignal.h>
#include <rapture/renderer/viewport/Viewport.h>
#include <rapture/scene/World.h>

class PlayLayer;

/**
 * @brief The workspace one world is built and played in
 */
class WorldEditorWorkspace : public AssetWorkspace {
  public:
    WorldEditorWorkspace(const EditorContext &context, Rapture::Ref<Rapture::AWorld> world);
    ~WorldEditorWorkspace() override;

    std::string_view tabIcon() const override;

  protected:
    void build() override;
    void buildHotbar() override;

  private:
    /**
     * @brief Creates the viewport this workspace draws its world into
     */
    void setupViewport();

    /**
     * @brief Builds the play, pause and step buttons into one hotbar item
     * @param group The hotbar item the buttons go in
     */
    void setupPlayControls(Amethyst::Frame &group);

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
    Amethyst::TextButton *m_addButton = nullptr;
    Amethyst::ImageButton *m_playButton = nullptr;
    Amethyst::ImageButton *m_pauseButton = nullptr;
    Amethyst::ImageButton *m_stepButton = nullptr;

    Rapture::Ref<Rapture::AWorld> m_world;
    PlayLayer *m_playLayer = nullptr;
    Rapture::EventConnection m_viewportClickedConn;
    Rapture::ViewportContext m_viewport;
};

#endif // RAPTURE__WORLD_EDITOR_WORKSPACE_H

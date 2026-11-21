#ifndef RAPTURE__WORLD_EDITOR_WORKSPACE_H
#define RAPTURE__WORLD_EDITOR_WORKSPACE_H

#include "AssetWorkspace.h"

#include <components/context_menu.h>
#include <rapture/assets/asset_manager/Asset.h>
#include <rapture/assets/worlds/AWorld.h>
#include <rapture/core/events/EventSignal.h>
#include <rapture/renderer/viewport/Viewport.h>
#include <rapture/scene/World.h>

#include <memory>

class EditorCamera;
class IconButton;
class PlayLayer;

/**
 * @brief The workspace one world is built and played in
 */
class WorldEditorWorkspace : public AssetWorkspace {
  public:
    std::string_view typeName() const override { return "WorldEditorWorkspace"; }
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
     * @brief Opens the menu of built-in shapes under the hotbar button, adding to the root of the scene
     * @param button The button the menu drops from
     */
    void showShapeMenu(Amethyst::TextButton &button);

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

    /**
     * @brief Shows or hides the viewport outline and hotbar note that mark the world as playing
     */
    void syncPlayingIndicators();

    /**
     * @brief Adds the hotbar note shown while the world plays
     */
    void addPlayingNote();

  private:
    Amethyst::ContextMenu *m_shapeMenu = nullptr;
    Amethyst::TextButton *m_addShapeButton = nullptr;
    IconButton *m_playButton = nullptr;
    IconButton *m_pauseButton = nullptr;
    IconButton *m_stepButton = nullptr;
    Amethyst::Frame *m_playingNote = nullptr;

    Rapture::Ref<Rapture::AWorld> m_world;
    PlayLayer *m_playLayer = nullptr;
    Rapture::EventConnection m_viewportClickedConn;
    std::unique_ptr<EditorCamera> m_editorCamera;
    Rapture::ViewportContext m_viewport;
};

#endif // RAPTURE__WORLD_EDITOR_WORKSPACE_H

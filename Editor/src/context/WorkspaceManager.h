#ifndef RAPTURE__WORKSPACE_MANAGER_H
#define RAPTURE__WORKSPACE_MANAGER_H

#include "EditorState.h"
#include "context/EditorContext.h"

#include <amethyst/Amethyst.h>
#include <rapture/core/events/EventSignal.h>

#include <memory>
#include <vector>

class AssetWorkspace;
class ScriptingWorkspace;
class Workspace;

/**
 * @brief Holds the open workspaces, one tab each in the workspace bar
 */
class WorkspaceManager {
  public:
    /**
     * @brief Creates a manager with no workspaces open
     * @param context What the whole editor reaches, which has to outlive the manager
     */
    explicit WorkspaceManager(const EditorContext &context);
    ~WorkspaceManager();

    WorkspaceManager(const WorkspaceManager &) = delete;
    WorkspaceManager &operator=(const WorkspaceManager &) = delete;

    /**
     * @brief Takes over the bar the workspaces are tabs of
     * @param tabBar The workspace bar
     */
    void attach(Amethyst::TabBar &tabBar);

    /**
     * @brief Tells every workspace where its tab's content sits in the window
     * @param position Top left corner of the content, in window pixels
     * @param size Size of the content, in window pixels
     */
    void setContentArea(Amethyst::vec2 position, Amethyst::vec2 size);

    /**
     * @brief Reopens the workspaces a previous run had open, always leaving the startup world open
     * @param state What the previous run stored
     */
    void restore(const EditorState &state);

    /**
     * @brief Which workspaces are open and which one is in front
     * @return The state to store for the next run
     */
    EditorState captureState() const;

    /**
     * @brief Writes out how every workspace's panels are arranged
     */
    void saveDockLayouts() const;

    void onUpdate(float dt);

    /**
     * @brief Opens an asset in the workspace its type calls for, focusing the tab it is already in
     * @param handle The asset to open
     */
    void openAsset(Rapture::AssetHandle handle);

    Workspace *activeWorkspace() const { return m_active; }

  public:
    /**
     * @brief Fires with the workspace whose tab just came to the front
     */
    Rapture::EventSignal<void(Workspace *)> onActiveWorkspaceChanged;

  private:
    /**
     * @brief Builds a workspace into a new tab and brings it to the front
     * @param workspace The workspace to take over
     * @return The workspace, which the host now owns
     */
    Workspace *adopt(std::unique_ptr<Workspace> workspace);

    /**
     * @brief Creates the workspace an asset's type calls for
     * @param handle The asset to create a workspace for
     * @return The workspace, nullptr when the asset cannot be opened
     */
    std::unique_ptr<Workspace> createAssetWorkspace(Rapture::AssetHandle handle) const;

    AssetWorkspace *findAssetWorkspace(Rapture::AssetHandle handle) const;
    ScriptingWorkspace *findScriptingWorkspace() const;

    /**
     * @brief Reopens one workspace a previous run had open, warning when this build cannot
     * @param entry The stored workspace to reopen
     */
    void openStoredWorkspace(const EditorWorkspaceState &entry);

    /**
     * @brief Brings a workspace's tab to the front
     * @param workspace The workspace to show
     */
    void select(Workspace &workspace);

    /**
     * @brief Marks the workspace in a tab as the one in front
     * @param content The tab's content frame
     */
    void onTabSelected(Amethyst::Instance *content);

    /**
     * @brief Destroys the workspace a closed tab held
     * @param content The closed tab's content frame
     */
    void closeWorkspace(Amethyst::Instance *content);

  private:
    const EditorContext *m_editor = nullptr;
    Amethyst::TabBar *m_tabBar = nullptr;
    std::vector<std::unique_ptr<Workspace>> m_workspaces;
    Workspace *m_active = nullptr;

    Amethyst::vec2 m_contentAreaPosition = {0.0f, 0.0f};
    Amethyst::vec2 m_contentAreaSize = {0.0f, 0.0f};
};

#endif // RAPTURE__WORKSPACE_MANAGER_H

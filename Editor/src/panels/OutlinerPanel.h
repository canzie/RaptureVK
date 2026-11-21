#ifndef RAPTURE__OUTLINER_PANEL_H
#define RAPTURE__OUTLINER_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/context_menu.h>
#include <components/tree_view.h>
#include <components/ui_scope.h>

#include "panels/Panel.h"
#include <cstdint>
#include <memory>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Instance.h>
#include <string>
#include <unordered_set>
#include <vector>

class OutlinerPanel : public Panel {
  public:
    std::string_view typeName() const override { return "OutlinerPanel"; }
    explicit OutlinerPanel(const WorkspaceContext &context);

    /**
     * @brief Update the outliner with the current scene
     */
    void setScene(Rapture::Scene *scene);

    /**
     * @brief Refresh the tree view with the current instance tree
     */
    void refresh(void);

    void onUpdate(float dt) override;

  protected:
    void build(Amethyst::UIScope &scope) override;
    void onRootDestroyed() override;

  private:
    /**
     * @brief Builds the search field and the add button beside it
     * @param scope The panel's root scope
     */
    void setupSearchBar(Amethyst::UIScope &scope);

    /**
     * @brief Opens the menu of scene objects to add at the root of the scene
     * @param pos Where the menu opens
     */
    void showAddMenu(Amethyst::vec2 pos);

    /**
     * @brief Opens the menu for the empty space below the rows, adding at the root of the scene or pasting there
     * @param pos Where the menu opens
     */
    void showEmptySpaceMenu(Amethyst::vec2 pos);

    /**
     * @brief The items that add a scene object at the root of the scene
     * @return The items, ready to hand to a ContextMenu
     */
    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> buildAddMenuItems() const;

    /**
     * @brief Moves the editor camera so the selected object fills the view
     */
    void focusOnSelection();

    void buildInstanceTree(Rapture::SceneObject *instance, Amethyst::TreeRowScope &rowScope);

    void onRowClicked(uint32_t row);
    void onRowRightClicked(uint32_t row, Amethyst::vec2 pos);

    void requestDelete(Rapture::SceneObject *instance, bool keepChildren);
    void applyPendingDelete(void);

    void showContextMenu(Amethyst::vec2 pos, std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items);

    void startRename(uint32_t row, Rapture::SceneObject *instance);
    void buildNameCell(uint32_t row, Rapture::SceneObject *instance, const std::string &name, bool editing);
    void onRenameCommitted(Rapture::SceneObject *instance, const std::string &newName);
    void applyPendingRename(void);

    Rapture::SceneObject *instanceForRow(uint32_t row) const;

    /**
     * @brief Highlights the row of an entity, or no row when it has none
     * @param entity The entity whose row is highlighted
     */
    void selectRowFor(Rapture::ecs::EntityAccessor entity);

  private:
    Amethyst::TreeView *m_treeView = nullptr;
    Amethyst::ContextMenu *m_contextMenu = nullptr;
    Amethyst::TextInput *m_renameInput = nullptr;

    Rapture::Scene *m_scene = nullptr;
    bool m_hasScene = false;

    /**
     * @brief Maps a TreeView logical row index to its instance, filled in DFS build order during refresh().
     */
    std::vector<Rapture::SceneObject *> m_rowInstances;
    std::unordered_set<const Rapture::SceneObject *> m_collapsedInstances;
    std::string m_loweredFilter;
    bool m_builtWithFilter = false;

    Rapture::SceneObject *m_renamingInstance = nullptr;
    uint32_t m_renameRow = 0;
    bool m_pendingRenameCommit = false;
    std::string m_pendingRenameName;

    Rapture::SceneObject *m_pendingDeleteInstance = nullptr;
    bool m_pendingDeleteKeepChildren = false;

    Rapture::EventConnection m_hierarchyChangedConn;
    Rapture::EventConnection m_selectionChangedConn;
    bool m_pendingRefresh = false;
};

#endif // RAPTURE__OUTLINER_PANEL_H

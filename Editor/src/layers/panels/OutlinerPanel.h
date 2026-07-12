#ifndef RAPTURE__OUTLINER_PANEL_H
#define RAPTURE__OUTLINER_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/context_menu.h>
#include <components/tree_view.h>
#include <components/ui_scope.h>

#include "layers/panels/Panel.h"
#include "scenes/Scene.h"
#include <cstdint>
#include <string>
#include <vector>

class OutlinerPanel : public Panel {
  public:
    OutlinerPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context);
    ~OutlinerPanel();
    OutlinerPanel(const OutlinerPanel &) = delete;
    OutlinerPanel &operator=(const OutlinerPanel &) = delete;
    OutlinerPanel(OutlinerPanel &&) = delete;
    OutlinerPanel &operator=(OutlinerPanel &&) = delete;

    /**
     * @brief Update the outliner with the current scene
     */
    void setScene(Rapture::Scene *scene);

    /**
     * @brief Refresh the tree view with current scene hierarchy
     */
    void refresh(void);

    void onUpdate(float dt) override;

  private:
    void buildEntityTree(Rapture::Entity entity, Amethyst::TreeRowScope &rowScope);

    void onRowClicked(uint32_t row);
    void onRowRightClicked(uint32_t row, Amethyst::vec2 pos);

    void requestDelete(uint32_t entityId, bool keepChildren);
    void applyPendingDelete(void);

    void showContextMenu(Amethyst::vec2 pos, std::vector<Amethyst::ContextMenuItem> items);

    void startRename(uint32_t row, uint32_t entityId);
    void buildNameCell(uint32_t row, uint32_t entityId, const std::string &name, bool editing);
    void onRenameCommitted(uint32_t entityId, const std::string &newName);
    void applyPendingRename(void);

    Rapture::Entity entityForRow(uint32_t row) const;

  private:
    Amethyst::Frame *m_root = nullptr;
    Amethyst::ScrollingFrame *m_scrollingFrame = nullptr;
    Amethyst::TreeView *m_treeView = nullptr;
    Amethyst::ContextMenu *m_contextMenu = nullptr;
    Amethyst::TextInput *m_renameInput = nullptr;

    Rapture::Scene *m_scene = nullptr;
    bool m_hasScene = false;

    /**
     * @brief Maps a TreeView logical row index to its entity id, filled in DFS build order during refresh().
     */
    std::vector<uint32_t> m_rowEntities;

    uint32_t m_renamingEntityId = UINT32_MAX;
    uint32_t m_renameRow = 0;
    bool m_pendingRenameCommit = false;
    std::string m_pendingRenameName;

    uint32_t m_pendingDeleteEntityId = UINT32_MAX;
    bool m_pendingDeleteKeepChildren = false;
};

#endif // RAPTURE__OUTLINER_PANEL_H

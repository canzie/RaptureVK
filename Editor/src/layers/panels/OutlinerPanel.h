#ifndef RAPTURE__OUTLINER_PANEL_H
#define RAPTURE__OUTLINER_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/context_menu.h>
#include <components/tree_view.h>
#include <components/ui_scope.h>

#include "layers/panels/Panel.h"
#include "scenes/Scene.h"
#include "scenes/instances/Instance.h"
#include <cstdint>
#include <memory>
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
     * @brief Refresh the tree view with the current instance tree
     */
    void refresh(void);

    void onUpdate(float dt) override;

  private:
    void buildInstanceTree(Rapture::Instance *instance, Amethyst::TreeRowScope &rowScope);

    void onRowClicked(uint32_t row);
    void onRowRightClicked(uint32_t row, Amethyst::vec2 pos);

    void requestDelete(Rapture::Instance *instance, bool keepChildren);
    void applyPendingDelete(void);

    void showContextMenu(Amethyst::vec2 pos, std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items);

    void startRename(uint32_t row, Rapture::Instance *instance);
    void buildNameCell(uint32_t row, Rapture::Instance *instance, const std::string &name, bool editing);
    void onRenameCommitted(Rapture::Instance *instance, const std::string &newName);
    void applyPendingRename(void);

    Rapture::Instance *instanceForRow(uint32_t row) const;

  private:
    Amethyst::ScrollingFrame *m_scrollingFrame = nullptr;
    Amethyst::TreeView *m_treeView = nullptr;
    Amethyst::ContextMenu *m_contextMenu = nullptr;
    Amethyst::TextInput *m_renameInput = nullptr;

    Rapture::Scene *m_scene = nullptr;
    bool m_hasScene = false;

    /**
     * @brief Maps a TreeView logical row index to its instance, filled in DFS build order during refresh().
     */
    std::vector<Rapture::Instance *> m_rowInstances;

    Rapture::Instance *m_renamingInstance = nullptr;
    uint32_t m_renameRow = 0;
    bool m_pendingRenameCommit = false;
    std::string m_pendingRenameName;

    Rapture::Instance *m_pendingDeleteInstance = nullptr;
    bool m_pendingDeleteKeepChildren = false;

    Rapture::EventConnection m_hierarchyChangedConn;
    bool m_pendingRefresh = false;
};

#endif // RAPTURE__OUTLINER_PANEL_H

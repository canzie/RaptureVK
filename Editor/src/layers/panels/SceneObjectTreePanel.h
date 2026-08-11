#ifndef RAPTURE__SCENE_OBJECT_TREE_PANEL_H
#define RAPTURE__SCENE_OBJECT_TREE_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/context_menu.h>
#include <components/tree_view.h>
#include <components/ui_scope.h>

#include "events/EventSignal.h"
#include "layers/panels/Panel.h"
#include "ecs/entity_accessor.h"

#include <cstdint>
#include <vector>

namespace Rapture {
class Instance;
class Scene;
} // namespace Rapture

/**
 * @brief The scene object tree below one root, with the menu that adds to it.
 *
 * Only the root's subtree is shown, so a scene holding more than what is being authored, such as
 * the camera a module editor looks through, stays out of it.
 */
class SceneObjectTreePanel : public Panel {
  public:
    /**
     * @brief Builds the panel over a subtree
     * @param tabBar The bar the panel's tab is added to
     * @param context The workspace the panel is built against
     * @param root The scene object whose subtree is shown, which cannot be deleted from here
     */
    SceneObjectTreePanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context, Rapture::Instance *root);
    ~SceneObjectTreePanel() override;
    SceneObjectTreePanel(const SceneObjectTreePanel &) = delete;
    SceneObjectTreePanel &operator=(const SceneObjectTreePanel &) = delete;

    /**
     * @brief Rebuilds the tree from the root's current subtree
     */
    void refresh(void);

    void onUpdate(float dt) override;

  private:
    void setupHeader(Amethyst::UIScope &scope);
    void setupTree(Amethyst::UIScope &scope);

    void buildSubtree(Rapture::Instance *instance, Amethyst::TreeRowScope &rowScope);

    /**
     * @brief The scene object an added object is parented to, which is the selected one or the root
     */
    Rapture::Instance *addTarget(void) const;
    void showAddMenu(Amethyst::vec2 pos);

    void onRowClicked(uint32_t row);
    void onRowRightClicked(uint32_t row, Amethyst::vec2 pos);

    /**
     * @brief Queues a scene object for destruction, which the context menu callback cannot do itself
     */
    void requestDelete(Rapture::Instance *instance);
    void applyPendingDelete(void);

    void selectRowFor(Rapture::ecs::EntityAccessor entity);

    Rapture::Instance *instanceForRow(uint32_t row) const;

  private:
    Amethyst::TreeView *m_treeView = nullptr;
    Amethyst::ContextMenu *m_contextMenu = nullptr;

    Rapture::Scene *m_scene = nullptr;
    Rapture::Instance *m_rootObject = nullptr;

    /**
     * @brief Maps a TreeView logical row index to its scene object, filled in DFS build order during refresh().
     */
    std::vector<Rapture::Instance *> m_rowObjects;

    Rapture::Instance *m_pendingDeleteObject = nullptr;

    Rapture::EventConnection m_hierarchyChangedConn;
    Rapture::EventConnection m_selectionChangedConn;
    bool m_pendingRefresh = false;
};

#endif // RAPTURE__SCENE_OBJECT_TREE_PANEL_H

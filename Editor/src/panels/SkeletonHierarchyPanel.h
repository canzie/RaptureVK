#ifndef RAPTURE__SKELETON_HIERARCHY_PANEL_H
#define RAPTURE__SKELETON_HIERARCHY_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/tree_view.h>
#include <components/ui_scope.h>

#include "core/ecs/entity_accessor.h"
#include "core/events/EventSignal.h"
#include "layers/panels/Panel.h"

#include <cstdint>
#include <vector>

namespace Rapture {
class SceneObject;
class SkeletonPose;
} // namespace Rapture

/**
 * @brief The bones of one posed skeleton, shown in the hierarchy they hang in.
 *
 * The bones a pose builds are internal to it, so unlike the outliner this shows what an object is
 * made of rather than only what was authored into the scene.
 */
class SkeletonHierarchyPanel : public Panel {
  public:
    /**
     * @brief Builds the panel over a pose
     * @param tabBar The bar the panel's tab is added to
     * @param context The workspace the panel is built against
     * @param pose The pose whose bones are shown
     */
    SkeletonHierarchyPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context, Rapture::SkeletonPose *pose);
    ~SkeletonHierarchyPanel() override;
    SkeletonHierarchyPanel(const SkeletonHierarchyPanel &) = delete;
    SkeletonHierarchyPanel &operator=(const SkeletonHierarchyPanel &) = delete;

    /**
     * @brief Rebuilds the rows from the pose's current bones
     */
    void refresh(void);

  private:
    void setupTreeView(Amethyst::UIScope &scope);

    /**
     * @brief Adds a row for a scene object and for everything below it, internal children included
     * @param object The object the row stands for
     * @param rowScope Scope of that row
     */
    void buildRows(Rapture::SceneObject *object, Amethyst::TreeRowScope &rowScope);

    void onRowClicked(uint32_t row);

    void selectRowFor(Rapture::ecs::EntityAccessor entity);

    Rapture::SceneObject *objectForRow(uint32_t row) const;

  private:
    Amethyst::TreeView *m_treeView = nullptr;
    Rapture::SkeletonPose *m_pose = nullptr;

    /**
     * @brief Maps a TreeView logical row index to the scene object it stands for
     */
    std::vector<Rapture::SceneObject *> m_rowObjects;

    Rapture::EventConnection m_selectionChangedConn;
};

#endif // RAPTURE__SKELETON_HIERARCHY_PANEL_H

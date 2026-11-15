#ifndef RAPTURE__SKELETON_WORKSPACE_H
#define RAPTURE__SKELETON_WORKSPACE_H

#include "PreviewWorkspace.h"
#include "gizmos/SkeletonGizmo.h"

#include <rapture/core/events/EventSignal.h>

#include <vector>

class ViewportPanel;

namespace Rapture {
class SkeletalMesh3D;
class SkeletonPose;
} // namespace Rapture

/**
 * @brief The workspace one open skeleton asset is edited in
 */
class SkeletonWorkspace : public PreviewWorkspace {
  public:
    SkeletonWorkspace(const EditorContext &context, Rapture::AssetHandle handle);

    void onUpdate(float dt) override;

    std::string_view tabIcon() const override;

  protected:
    void build() override;
    void buildHotbar() override;

  private:
    /**
     * @brief Creates the pose the open skeleton is shown through, and watches what it is shown on
     */
    void setupPose();

    /**
     * @brief Replaces the spawned meshes with the ones the open skeleton is shown on
     */
    void spawnPreviewMeshes();

  private:
    Rapture::SkeletonPose *m_pose = nullptr;
    ViewportPanel *m_viewportPanel = nullptr;
    gizmo::SkeletonGizmo m_skeletonGizmo{EDITOR_MODE_OBJECT};
    std::vector<Rapture::SkeletalMesh3D *> m_previewObjects;
    Rapture::EventConnection m_previewMeshesChangedConn;
    Rapture::EventConnection m_modeChangedConn;
    bool m_previewMeshesPending = false;
};

#endif // RAPTURE__SKELETON_WORKSPACE_H

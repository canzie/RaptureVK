#ifndef RAPTURE__SKELETAL_MESH_WORKSPACE_H
#define RAPTURE__SKELETAL_MESH_WORKSPACE_H

#include "PreviewWorkspace.h"

namespace Rapture {
class SkeletalMesh3D;
class SkeletonPose;
} // namespace Rapture

/**
 * @brief The workspace one open skeletal mesh asset is inspected in
 */
class SkeletalMeshWorkspace : public PreviewWorkspace {
  public:
    std::string_view typeName() const override { return "SkeletalMeshWorkspace"; }
    SkeletalMeshWorkspace(const EditorContext &context, Rapture::AssetHandle handle);

    std::string_view tabIcon() const override;

  protected:
    void build() override;
    void buildHotbar() override;

  private:
    /**
     * @brief Creates the pose of the skeleton the open mesh is bound to, which the mesh is drawn against
     */
    void setupPose();

    /**
     * @brief Imports the open mesh again from the file it came from
     */
    void reimport();

  private:
    Rapture::SkeletalMesh3D *m_mesh = nullptr;
    Rapture::SkeletonPose *m_pose = nullptr;
};

#endif // RAPTURE__SKELETAL_MESH_WORKSPACE_H

#ifndef RAPTURE__SKELETON_WORKSPACE_H
#define RAPTURE__SKELETON_WORKSPACE_H

#include "AssetPreviewWorkspace.h"

/**
 * @brief The workspace one open skeleton asset is edited in
 */
class SkeletonWorkspace : public AssetPreviewWorkspace {
  public:
    SkeletonWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services, Rapture::AssetHandle handle);

    static constexpr std::string_view staticKind() { return "skeleton"; }

  private:
    /**
     * @brief Spawns the meshes the open skeleton is shown on
     */
    void spawnPreviewMeshes();
};

#endif // RAPTURE__SKELETON_WORKSPACE_H

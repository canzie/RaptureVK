#ifndef RAPTURE__SKELETAL_MESH_WORKSPACE_H
#define RAPTURE__SKELETAL_MESH_WORKSPACE_H

#include "AssetPreviewWorkspace.h"

/**
 * @brief The workspace one open skeletal mesh asset is inspected in
 */
class SkeletalMeshWorkspace : public AssetPreviewWorkspace {
  public:
    SkeletalMeshWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services, Rapture::AssetHandle handle);

    static constexpr std::string_view staticKind() { return "skeletalMesh"; }

  private:
    void setupHotbar();
};

#endif // RAPTURE__SKELETAL_MESH_WORKSPACE_H

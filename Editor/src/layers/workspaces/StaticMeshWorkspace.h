#ifndef RAPTURE__STATIC_MESH_WORKSPACE_H
#define RAPTURE__STATIC_MESH_WORKSPACE_H

#include "AssetPreviewWorkspace.h"

/**
 * @brief The workspace one open static mesh asset is inspected in
 */
class StaticMeshWorkspace : public AssetPreviewWorkspace {
  public:
    StaticMeshWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services, Rapture::AssetHandle handle);

    static constexpr std::string_view staticKind() { return "staticMesh"; }
};

#endif // RAPTURE__STATIC_MESH_WORKSPACE_H

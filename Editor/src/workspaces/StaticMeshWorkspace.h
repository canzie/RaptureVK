#ifndef RAPTURE__STATIC_MESH_WORKSPACE_H
#define RAPTURE__STATIC_MESH_WORKSPACE_H

#include "PreviewWorkspace.h"

/**
 * @brief The workspace one open static mesh asset is inspected in
 */
class StaticMeshWorkspace : public PreviewWorkspace {
  public:
    std::string_view typeName() const override { return "StaticMeshWorkspace"; }
    StaticMeshWorkspace(const EditorContext &context, Rapture::AssetHandle handle);

    std::string_view tabIcon() const override;

  protected:
    void build() override;
    void buildHotbar() override {}
};

#endif // RAPTURE__STATIC_MESH_WORKSPACE_H

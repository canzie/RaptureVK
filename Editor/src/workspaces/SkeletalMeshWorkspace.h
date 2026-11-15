#ifndef RAPTURE__SKELETAL_MESH_WORKSPACE_H
#define RAPTURE__SKELETAL_MESH_WORKSPACE_H

#include "PreviewWorkspace.h"

/**
 * @brief The workspace one open skeletal mesh asset is inspected in
 */
class SkeletalMeshWorkspace : public PreviewWorkspace {
  public:
    SkeletalMeshWorkspace(const EditorContext &context, Rapture::AssetHandle handle);

    std::string_view tabIcon() const override;

  protected:
    void build() override;
    void buildHotbar() override;
};

#endif // RAPTURE__SKELETAL_MESH_WORKSPACE_H

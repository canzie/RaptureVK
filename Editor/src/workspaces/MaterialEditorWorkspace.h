#ifndef RAPTURE__MATERIAL_EDITOR_WORKSPACE_H
#define RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

#include "PreviewWorkspace.h"

#include <rapture/assets/asset_manager/Asset.h>
#include <rapture/core/events/EventSignal.h>

class NodeEditorPanel;

namespace Rapture {
class StaticMesh3D;
} // namespace Rapture

/**
 * @brief The workspace one open material is authored in, shown on a sphere beside its graph
 */
class MaterialEditorWorkspace : public PreviewWorkspace {
  public:
    std::string_view typeName() const override { return "MaterialEditorWorkspace"; }
    MaterialEditorWorkspace(const EditorContext &context, Rapture::AssetHandle handle);

    std::string_view tabIcon() const override;

  protected:
    void build() override;
    void buildHotbar() override;

  private:
    /**
     * @brief Binds a selected material instance onto the preview sphere
     * @param handle The material asset to show
     */
    void showMaterialOnSphere(Rapture::AssetHandle handle);

  private:
    NodeEditorPanel *m_nodeEditor = nullptr;
    Rapture::StaticMesh3D *m_previewSphere = nullptr;
    Rapture::EventConnection m_materialSelectedConn;
    Rapture::AssetRef m_skybox;
};

#endif // RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

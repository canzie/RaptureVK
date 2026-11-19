#ifndef RAPTURE__MATERIAL_EDITOR_WORKSPACE_H
#define RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

#include "PreviewWorkspace.h"

#include <rapture/assets/asset_manager/Asset.h>
#include <rapture/core/events/EventSignal.h>

class NodeEditorPanel;

namespace Amethyst {
class Frame;
class TextLabel;
} // namespace Amethyst

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

    /**
     * @brief What the hotbar says about the graph's last compile
     */
    enum CompileStatus {
        COMPILE_STATUS_COMPILED,
        COMPILE_STATUS_CHANGED,
        COMPILE_STATUS_FAILED,
        COMPILE_STATUS_COUNT
    };

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

    /**
     * @brief Shows the graph's compile status in the hotbar
     * @param status The status
     */
    void showCompileStatus(CompileStatus status);

  private:
    NodeEditorPanel *m_nodeEditor = nullptr;
    Amethyst::Frame *m_compileStatusDot = nullptr;
    Amethyst::TextLabel *m_compileStatusLabel = nullptr;
    Rapture::EventConnection m_compiledConn;
    Rapture::EventConnection m_nodeEditedConn;
    Rapture::EventConnection m_nodeValuesConn;
    Rapture::StaticMesh3D *m_previewSphere = nullptr;
    Rapture::EventConnection m_materialSelectedConn;
    Rapture::AssetRef m_skybox;
};

#endif // RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

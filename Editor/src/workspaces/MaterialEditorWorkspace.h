#ifndef RAPTURE__MATERIAL_EDITOR_WORKSPACE_H
#define RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

#include "PreviewWorkspace.h"

#include <core/ecs/entity_accessor.h>
#include <core/events/EventSignal.h>

class NodeEditorPanel;

/**
 * @brief The workspace one open material is authored in, shown on a sphere beside its graph
 */
class MaterialEditorWorkspace : public PreviewWorkspace {
  public:
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
    Rapture::ecs::EntityAccessor m_previewSphere;
    Rapture::EventConnection m_materialSelectedConn;
};

#endif // RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

#ifndef RAPTURE__MATERIAL_EDITOR_WORKSPACE_H
#define RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

#include "Workspace.h"

#include <asset_manager/AssetCommon.h>
#include <events/EventSignal.h>
#include <scenes/entities/Entity.h>

namespace Rapture {
class Scene;
class Viewport;
}

class NodeEditorPanel;

class MaterialEditorWorkspace : public Workspace {
  public:
    MaterialEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services);
    ~MaterialEditorWorkspace() override;

    void saveLayout() override;

  private:
    void setupHotbar(void);
    void setupPreviewScene(void);

    /**
     * @brief Binds a selected material instance onto the preview sphere
     * @param handle The material asset to show
     */
    void showMaterialOnSphere(Rapture::AssetHandle handle);

    NodeEditorPanel *m_nodeEditor = nullptr;

    Rapture::Scene *m_previewScene = nullptr;
    Rapture::Viewport *m_previewViewport = nullptr;
    Rapture::Entity m_previewSphere;
    Rapture::EventConnection m_materialSelectedConn;
};

#endif // RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

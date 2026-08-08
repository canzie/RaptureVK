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
    /**
     * @brief Opens a material asset in a tab of its own.
     * @param tabBar The workspace bar the tab is appended to.
     * @param services Services the panels are built against.
     * @param handle The material asset this workspace edits.
     */
    MaterialEditorWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services, Rapture::AssetHandle handle);
    ~MaterialEditorWorkspace() override;

    void onUpdate(float dt) override;
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

    Rapture::AssetHandle m_handle = Rapture::INVALID_ASSET_HANDLE;
    std::unique_ptr<Rapture::Scene> m_previewScene;
    Rapture::Viewport *m_previewViewport = nullptr;
    Rapture::Entity m_previewSphere;
    Rapture::EventConnection m_materialSelectedConn;
};

#endif // RAPTURE__MATERIAL_EDITOR_WORKSPACE_H

#ifndef RAPTURE__ASSET_PREVIEW_WORKSPACE_H
#define RAPTURE__ASSET_PREVIEW_WORKSPACE_H

#include "Workspace.h"

#include <assets/asset_manager/AssetCommon.h>
#include <renderer/viewport/Viewport.h>

#include <glm/glm.hpp>

#include <memory>

namespace Rapture {
class Scene;
} // namespace Rapture

/**
 * @brief Base of the workspaces that open one asset and show it in a scene of their own.
 *
 * The scene is thrown away with the workspace, so nothing spawned into it is authored content.
 */
class AssetPreviewWorkspace : public Workspace {
  public:
    ~AssetPreviewWorkspace() override;

    void onUpdate(float dt) override;
    void saveLayout() override;

  protected:
    AssetPreviewWorkspace(std::string_view kind, const PanelServices &services, Rapture::AssetHandle handle);

    /**
     * @brief Builds the scene and viewport this workspace shows its asset in
     * @param sceneName Name the preview scene is created under
     */
    void setupPreviewScene(std::string_view sceneName);

    /**
     * @brief Names and styles this workspace's docking layer after the open asset
     */
    void setupDockingLayer();

    /**
     * @brief Puts this workspace's docking layer back the way it was last saved
     */
    void applyStoredLayout();

    /**
     * @brief Frames the camera on a box, so what this workspace opened fills the view
     * @param min Corner of the world space box to frame
     * @param max Opposite corner of the world space box to frame
     */
    void setFocusBounds(const glm::vec3 &min, const glm::vec3 &max);

    Rapture::Scene *previewScene() const { return m_previewScene.get(); }
    Rapture::AssetHandle handle() const { return m_handle; }

    /**
     * @brief The name the open asset goes by, for the tab and the panels
     * @return The asset's name
     */
    std::string_view assetName() const;

  private:
    std::unique_ptr<Rapture::Scene> m_previewScene;
    Rapture::ViewportContext m_previewViewport;
    Rapture::AssetHandle m_handle = Rapture::INVALID_ASSET_HANDLE;
};

#endif // RAPTURE__ASSET_PREVIEW_WORKSPACE_H

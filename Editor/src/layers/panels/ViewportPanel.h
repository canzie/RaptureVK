#ifndef RAPTURE__VIEWPORT_PANEL_H
#define RAPTURE__VIEWPORT_PANEL_H

#include <amethyst/Amethyst.h>
#include <amethyst__vk13_glfw.h>
#include <components/ui_scope.h>
#include <components/widgets/gizmo.h>

#include "layers/panels/Panel.h"
#include "scenes/entities/Entity.h"

#include <math/math.h>
#include <memory>

namespace Rapture {
class CameraController;
}

class ViewportPanel : public Panel {
  public:
    ViewportPanel(Amethyst::TabBar *tabBar);
    ~ViewportPanel();
    ViewportPanel(const ViewportPanel &) = delete;
    ViewportPanel &operator=(const ViewportPanel &) = delete;
    ViewportPanel(ViewportPanel &&) = delete;
    ViewportPanel &operator=(ViewportPanel &&) = delete;

    void setViewportImage(Amethyst::AmTextureId imageId);
    void onUpdate(float dt) override;

    Amethyst::GizmoOperation getGizmoOperation() const { return m_gizmoOperation; }
    void setGizmoOperation(Amethyst::GizmoOperation op) { m_gizmoOperation = op; }
    Amethyst::GizmoSpace getGizmoSpace() const { return m_gizmoSpace; }
    void setGizmoSpace(Amethyst::GizmoSpace space) { m_gizmoSpace = space; }

  private:
    void updateGizmo();
    void setupHeader(Amethyst::FrameScope &f);
    void syncCameraModeButton();
    Rapture::CameraController *cameraController() const;

  private:
    Amethyst::Frame *m_root = nullptr;
    Amethyst::ImageLabel *m_viewportImage = nullptr;
    Amethyst::Frame *m_header = nullptr;

    Amethyst::EventConnection m_viewportImageDestroyConn;

    Amethyst::TextButton *m_translateBtn = nullptr;
    Amethyst::TextButton *m_rotateBtn = nullptr;
    Amethyst::TextButton *m_scaleBtn = nullptr;
    Amethyst::TextButton *m_spaceBtn = nullptr;
    Amethyst::TextButton *m_cameraModeBtn = nullptr;
    bool m_viewportHovered = false;

    std::unique_ptr<Amethyst::Gizmo> m_gizmo;
    Amethyst::GizmoOperation m_gizmoOperation = Amethyst::GizmoOperation::TRANSLATE;
    Amethyst::GizmoSpace m_gizmoSpace = Amethyst::GizmoSpace::WORLD;

    std::shared_ptr<Rapture::Entity> m_selectedEntity;
    std::shared_ptr<Rapture::Entity> m_previousSelectedEntity;
    size_t m_entitySelectedListenerId = 0;
    Amethyst::vec2 m_lastViewportSize = {};
    Amethyst::vec2 m_pendingViewportSize = {};
    float m_resizeStableTime = 0.0f;
    bool m_resizePending = false;
};

#endif // RAPTURE__VIEWPORT_PANEL_H

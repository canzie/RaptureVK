#ifndef RAPTURE__VIEWPORT_PANEL_H
#define RAPTURE__VIEWPORT_PANEL_H

#include <amethyst/Amethyst.h>
#include <amethyst__vk13_glfw.h>
#include <components/common.h>
#include <components/docking_layer.h>
#include <components/frame.h>
#include <components/image_label.h>
#include <components/panel_layer.h>
#include <components/text_button.h>
#include <components/widgets/gizmo.h>

#include "scenes/entities/Entity.h"

#include <memory>

class ViewportPanel {
  public:
    ViewportPanel(Amethyst::DockingLayer *dockingLayer);
    ~ViewportPanel();
    ViewportPanel(const ViewportPanel &) = delete;
    ViewportPanel &operator=(const ViewportPanel &) = delete;
    ViewportPanel(ViewportPanel &&) = delete;
    ViewportPanel &operator=(ViewportPanel &&) = delete;

    void setViewportImage(Amethyst::AmTextureId imageId);
    void onUpdate();

    Amethyst::GizmoOperation getGizmoOperation() const { return m_gizmoOperation; }
    void setGizmoOperation(Amethyst::GizmoOperation op) { m_gizmoOperation = op; }
    Amethyst::GizmoSpace getGizmoSpace() const { return m_gizmoSpace; }
    void setGizmoSpace(Amethyst::GizmoSpace space) { m_gizmoSpace = space; }

  private:
    void updateGizmo();
    void setupHeader();

  private:
    Amethyst::DockingLayer *m_dockingLayer = nullptr;
    Amethyst::PanelLayer *m_root = nullptr;
    Amethyst::ImageLabel *m_viewportImage = nullptr;
    Amethyst::Frame *m_header = nullptr;

    Amethyst::TextButton *m_translateBtn = nullptr;
    Amethyst::TextButton *m_rotateBtn = nullptr;
    Amethyst::TextButton *m_scaleBtn = nullptr;
    Amethyst::TextButton *m_spaceBtn = nullptr;

    std::unique_ptr<Amethyst::Gizmo> m_gizmo;
    Amethyst::GizmoOperation m_gizmoOperation = Amethyst::GizmoOperation::TRANSLATE;
    Amethyst::GizmoSpace m_gizmoSpace = Amethyst::GizmoSpace::WORLD;

    std::shared_ptr<Rapture::Entity> m_selectedEntity;
    std::shared_ptr<Rapture::Entity> m_previousSelectedEntity;
    size_t m_entitySelectedListenerId = 0;
};

#endif // RAPTURE__VIEWPORT_PANEL_H

#ifndef RAPTURE__VIEWPORT_PANEL_H
#define RAPTURE__VIEWPORT_PANEL_H

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include "imguiPanels/modules/Gizmo.h"

#include "Scenes/Entities/Entity.h"
#include "Scenes/Scene.h"

#include <memory>

class ViewportPanel {
  public:
    ViewportPanel();
    ~ViewportPanel();

    void renderSceneViewport(ImTextureID textureID);

    // Gizmo state controls
    Modules::Gizmo::Operation getCurrentGizmoOperation() const { return m_currentGizmoOperation; }
    void setCurrentGizmoOperation(Modules::Gizmo::Operation op) { m_currentGizmoOperation = op; }
    Modules::Gizmo::Space getCurrentGizmoSpace() const { return m_currentGizmoSpace; }
    void setCurrentGizmoSpace(Modules::Gizmo::Space space) { m_currentGizmoSpace = space; }
    void toggleGizmoSpace()
    {
        m_currentGizmoSpace =
            (m_currentGizmoSpace == Modules::Gizmo::Space::WORLD) ? Modules::Gizmo::Space::LOCAL : Modules::Gizmo::Space::WORLD;
    }

    // Render gizmo for the selected entity
    void renderEntityGizmo();

    void setVisible(bool visible) { m_isVisible = visible; }
    bool isVisible() const { return m_isVisible; }

    // Get current viewport size (actual image area, excluding topbar)
    ImVec2 getViewportSize() const { return m_viewportSize; }
    ImVec2 getViewportPosition() const { return m_viewportPosition; }

  private:
    void renderTopbar();
    void checkForSizeChange();

  private:
    static constexpr float TOPBAR_HEIGHT = 40.0f;

    bool m_isVisible = true;

    ImVec2 m_viewportPosition; // Actual viewport image position (after topbar)
    ImVec2 m_viewportSize;     // Actual viewport image size (excluding topbar)
    ImVec2 m_lastViewportSize; // Previous frame's size for change detection

    // Gizmo
    Modules::Gizmo::Gizmo m_gizmo;
    Modules::Gizmo::Operation m_currentGizmoOperation = Modules::Gizmo::Operation::TRANSLATE;
    Modules::Gizmo::Space m_currentGizmoSpace = Modules::Gizmo::Space::WORLD;

    // Entity selection
    std::shared_ptr<Rapture::Entity> m_selectedEntity;
    std::shared_ptr<Rapture::Entity> m_previousSelectedEntity;
    size_t m_entitySelectedListenerId = 0;
};

#endif // RAPTURE__VIEWPORT_PANEL_H

#ifndef RAPTURE__VIEWPORT_PANEL_H
#define RAPTURE__VIEWPORT_PANEL_H

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include "vendor/ImGuizmo/ImGuizmo.h"

#include "Scenes/Entities/Entity.h"
#include "Scenes/Scene.h"

#include <memory>

class ViewportPanel {
  public:
    ViewportPanel();
    ~ViewportPanel();

    void renderSceneViewport(ImTextureID textureID);

    // ImGuizmo state controls
    ImGuizmo::OPERATION getCurrentGizmoOperation() const { return m_currentGizmoOperation; }
    ImGuizmo::MODE getCurrentGizmoMode() const { return m_currentGizmoMode; }

    // Add a method to render ImGuizmo for the selected entity
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

    // ImGuizmo state
    ImGuizmo::OPERATION m_currentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE m_currentGizmoMode = ImGuizmo::WORLD;

    // Entity selection
    std::shared_ptr<Rapture::Entity> m_selectedEntity;
    size_t m_entitySelectedListenerId = 0;
};

#endif // RAPTURE__VIEWPORT_PANEL_H

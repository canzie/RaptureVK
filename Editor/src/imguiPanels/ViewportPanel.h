#ifndef VIEWPORT_PANEL_H
#define VIEWPORT_PANEL_H

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "vendor/ImGuizmo/ImGuizmo.h"


#include "Scenes/Scene.h"
#include "Scenes/Entities/Entity.h"

#include <functional>
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

    // Get current viewport size
    ImVec2 getViewportSize() const { return m_viewportSize; }

private:
    void checkForSizeChange();

private:
    bool m_isVisible = true;

    ImVec2 m_viewportPosition;  // Window position
    ImVec2 m_viewportSize;      // Window size
    ImVec2 m_lastViewportSize;  // Previous frame's size for change detection

    // ImGuizmo state
    ImGuizmo::OPERATION m_currentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE m_currentGizmoMode = ImGuizmo::WORLD;
    
    // Entity selection
    std::shared_ptr<Rapture::Entity> m_selectedEntity;
    size_t m_entitySelectedListenerId = 0;
};

#endif // VIEWPORT_PANEL_H


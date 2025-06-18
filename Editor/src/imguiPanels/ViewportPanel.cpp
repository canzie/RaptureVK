#include "ViewportPanel.h"
#include "Logging/TracyProfiler.h"
#include "Logging/Log.h"

#include "Components/Components.h"

#include "Scenes/SceneManager.h"
#include "imguiPanelStyleLinear.h"

#include "Events/GameEvents.h"

#include <string>

ViewportPanel::ViewportPanel() {

    m_entitySelectedListenerId = Rapture::GameEvents::onEntitySelected().addListener(
        [this](std::shared_ptr<Rapture::Entity> entity) {
            m_selectedEntity = entity;
    });

}

ViewportPanel::~ViewportPanel() {

    Rapture::GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId);
}

void ViewportPanel::renderSceneViewport(ImTextureID textureID)
{
    RAPTURE_PROFILE_FUNCTION();

    if (!m_isVisible) {
        return;
    }

    std::string title = "Viewport " + std::string(ICON_MD_WEB_ASSET);

    ImGui::Begin(title.c_str());

    m_viewportPosition = ImGui::GetCursorScreenPos();
    m_viewportSize = ImGui::GetContentRegionAvail();


    ImVec2 windowSize = ImGui::GetContentRegionAvail();
    ImGui::Image((ImTextureID)textureID, windowSize);

            // Calculate position for the gizmo controls overlay
            ImVec2 windowPos = ImGui::GetWindowPos();
            ImVec2 controlPos(
                windowPos.x + ImGui::GetWindowWidth() - 200, // Move further left
                windowPos.y + 40                            // Keep the same vertical position
            );
            
            // Save the current cursor position
            ImVec2 origCursorPos = ImGui::GetCursorPos();
            
            // Begin overlay for gizmo controls
            ImGui::SetNextWindowPos(controlPos);
            ImGui::SetNextWindowBgAlpha(0.85f); // Less transparent background
            ImGui::BeginChild("GizmoControls", ImVec2(160, 60), true, // Wider panel
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
            
            bool isTranslate = m_currentGizmoOperation == ImGuizmo::TRANSLATE;
            bool isRotate = m_currentGizmoOperation == ImGuizmo::ROTATE;
            bool isScale = m_currentGizmoOperation == ImGuizmo::SCALE;
            
            // Get the background and hover colors from style
            ImVec4 defaultBgColor = ImGuiPanelStyle::BACKGROUND_TERTIARY;
            ImVec4 selectedBgColor = ImGuiPanelStyle::ACCENT_PRIMARY;
            ImVec4 textColor = ImGuiPanelStyle::TEXT_NORMAL;
            ImVec4 hoverColor = ImGuiPanelStyle::ACCENT_PRIMARY;
            ImVec4 accentColor = ImGuiPanelStyle::ACCENT_PRIMARY;
            
            // Button size
            const float buttonSize = 39.0f;
            const float iconSize = 28.0f;
            const float spacing = 52.0f;
            float posX = ImGui::GetCursorPosX();
            
            // Translate button with icon
            if (isTranslate) ImGui::PushStyleColor(ImGuiCol_Button, accentColor);
            else ImGui::PushStyleColor(ImGuiCol_Button, defaultBgColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            
            bool translateClicked = ImGui::Button("##Translate", ImVec2(buttonSize, buttonSize));
            
            // Draw translate icon (arrows pointing outward)
            ImVec2 buttonMin = ImGui::GetItemRectMin();
            ImVec2 buttonMax = ImGui::GetItemRectMax();
            ImVec2 buttonCenter = ImVec2((buttonMin.x + buttonMax.x) * 0.5f, (buttonMin.y + buttonMax.y) * 0.5f);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            
            // Draw 3 arrows for translate (X, Y, Z)
            float arrowSize = iconSize * 0.5f;
            // X arrow (red)
            drawList->AddLine(
                ImVec2(buttonCenter.x - arrowSize * 0.5f, buttonCenter.y),
                ImVec2(buttonCenter.x + arrowSize, buttonCenter.y),
                IM_COL32(255, 50, 50, 255), 2.0f);
            drawList->AddTriangleFilled(
                ImVec2(buttonCenter.x + arrowSize, buttonCenter.y - arrowSize * 0.3f),
                ImVec2(buttonCenter.x + arrowSize, buttonCenter.y + arrowSize * 0.3f),
                ImVec2(buttonCenter.x + arrowSize * 1.5f, buttonCenter.y),
                IM_COL32(255, 50, 50, 255));
                
            // Y arrow (green)
            drawList->AddLine(
                ImVec2(buttonCenter.x, buttonCenter.y + arrowSize * 0.5f),
                ImVec2(buttonCenter.x, buttonCenter.y - arrowSize),
                IM_COL32(50, 255, 50, 255), 2.0f);
            drawList->AddTriangleFilled(
                ImVec2(buttonCenter.x - arrowSize * 0.3f, buttonCenter.y - arrowSize),
                ImVec2(buttonCenter.x + arrowSize * 0.3f, buttonCenter.y - arrowSize),
                ImVec2(buttonCenter.x, buttonCenter.y - arrowSize * 1.5f),
                IM_COL32(50, 255, 50, 255));
                
            // Z arrow (blue, at 45 degrees)
            drawList->AddLine(
                ImVec2(buttonCenter.x - arrowSize * 0.35f, buttonCenter.y + arrowSize * 0.35f),
                ImVec2(buttonCenter.x - arrowSize * 0.7f, buttonCenter.y + arrowSize * 0.7f),
                IM_COL32(50, 50, 255, 255), 2.0f);
            drawList->AddTriangleFilled(
                ImVec2(buttonCenter.x - arrowSize * 0.7f - arrowSize * 0.2f, buttonCenter.y + arrowSize * 0.7f),
                ImVec2(buttonCenter.x - arrowSize * 0.7f, buttonCenter.y + arrowSize * 0.7f + arrowSize * 0.2f),
                ImVec2(buttonCenter.x - arrowSize * 0.7f - arrowSize * 0.3f, buttonCenter.y + arrowSize * 0.7f + arrowSize * 0.3f),
                IM_COL32(50, 50, 255, 255));
            
            ImGui::PopStyleColor(2);
            
            if (translateClicked) {
                m_currentGizmoOperation = ImGuizmo::TRANSLATE;
                Rapture::RP_INFO("Gizmo operation set to Translate");
            }
            
            ImGui::SameLine(posX + spacing);
            
            // Rotate button with icon
            if (isRotate) ImGui::PushStyleColor(ImGuiCol_Button, accentColor);
            else ImGui::PushStyleColor(ImGuiCol_Button, defaultBgColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            
            bool rotateClicked = ImGui::Button("##Rotate", ImVec2(buttonSize, buttonSize));
            
            // Draw rotate icon (circular arrows)
            buttonMin = ImGui::GetItemRectMin();
            buttonMax = ImGui::GetItemRectMax();
            buttonCenter = ImVec2((buttonMin.x + buttonMax.x) * 0.5f, (buttonMin.y + buttonMax.y) * 0.5f);
            
            float radius = iconSize * 0.4f;
            int segments = 16;
            drawList->AddCircle(buttonCenter, radius, IM_COL32(255, 255, 255, 200), segments, 2.0f);
            
            // Add arrow head at the end of the circle
            float angle = 0.75f * 3.14159f * 2.0f; // About 270 degrees
            float arrowX = buttonCenter.x + radius * cosf(angle);
            float arrowY = buttonCenter.y + radius * sinf(angle);
            
            float arrowTipSize = 4.0f;
            drawList->AddTriangleFilled(
                ImVec2(arrowX, arrowY),
                ImVec2(arrowX + arrowTipSize * cosf(angle + 2.5f), arrowY + arrowTipSize * sinf(angle + 2.5f)),
                ImVec2(arrowX + arrowTipSize * cosf(angle - 2.5f), arrowY + arrowTipSize * sinf(angle - 2.5f)),
                IM_COL32(255, 255, 255, 200));
            
            ImGui::PopStyleColor(2);
            
            if (rotateClicked) {
                m_currentGizmoOperation = ImGuizmo::ROTATE;
                Rapture::RP_INFO("Gizmo operation set to Rotate");
            }
            
            ImGui::SameLine(posX + spacing * 2);
            
            // Scale button with icon
            if (isScale) ImGui::PushStyleColor(ImGuiCol_Button, accentColor);
            else ImGui::PushStyleColor(ImGuiCol_Button, defaultBgColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            
            bool scaleClicked = ImGui::Button("##Scale", ImVec2(buttonSize, buttonSize));
            
            // Draw scale icon (expanding box)
            buttonMin = ImGui::GetItemRectMin();
            buttonMax = ImGui::GetItemRectMax();
            buttonCenter = ImVec2((buttonMin.x + buttonMax.x) * 0.5f, (buttonMin.y + buttonMax.y) * 0.5f);
            
            float boxSize = iconSize * 0.4f;
            drawList->AddRect(
                ImVec2(buttonCenter.x - boxSize, buttonCenter.y - boxSize),
                ImVec2(buttonCenter.x + boxSize, buttonCenter.y + boxSize),
                IM_COL32(255, 255, 255, 200), 0.0f, 0, 2.0f);
            
            // Draw corner handles
            float handleSize = 3.0f;
            // Top-left corner
            drawList->AddLine(
                ImVec2(buttonCenter.x - boxSize - handleSize, buttonCenter.y - boxSize),
                ImVec2(buttonCenter.x - boxSize, buttonCenter.y - boxSize),
                IM_COL32(255, 255, 255, 200), 2.0f);
            drawList->AddLine(
                ImVec2(buttonCenter.x - boxSize, buttonCenter.y - boxSize - handleSize),
                ImVec2(buttonCenter.x - boxSize, buttonCenter.y - boxSize),
                IM_COL32(255, 255, 255, 200), 2.0f);
            
            // Top-right corner
            drawList->AddLine(
                ImVec2(buttonCenter.x + boxSize + handleSize, buttonCenter.y - boxSize),
                ImVec2(buttonCenter.x + boxSize, buttonCenter.y - boxSize),
                IM_COL32(255, 255, 255, 200), 2.0f);
            drawList->AddLine(
                ImVec2(buttonCenter.x + boxSize, buttonCenter.y - boxSize - handleSize),
                ImVec2(buttonCenter.x + boxSize, buttonCenter.y - boxSize),
                IM_COL32(255, 255, 255, 200), 2.0f);
            
            // Bottom-left corner
            drawList->AddLine(
                ImVec2(buttonCenter.x - boxSize - handleSize, buttonCenter.y + boxSize),
                ImVec2(buttonCenter.x - boxSize, buttonCenter.y + boxSize),
                IM_COL32(255, 255, 255, 200), 2.0f);
            drawList->AddLine(
                ImVec2(buttonCenter.x - boxSize, buttonCenter.y + boxSize + handleSize),
                ImVec2(buttonCenter.x - boxSize, buttonCenter.y + boxSize),
                IM_COL32(255, 255, 255, 200), 2.0f);
            
            // Bottom-right corner
            drawList->AddLine(
                ImVec2(buttonCenter.x + boxSize + handleSize, buttonCenter.y + boxSize),
                ImVec2(buttonCenter.x + boxSize, buttonCenter.y + boxSize),
                IM_COL32(255, 255, 255, 200), 2.0f);
            drawList->AddLine(
                ImVec2(buttonCenter.x + boxSize, buttonCenter.y + boxSize + handleSize),
                ImVec2(buttonCenter.x + boxSize, buttonCenter.y + boxSize),
                IM_COL32(255, 255, 255, 200), 2.0f);
            
            ImGui::PopStyleColor(2);
            
            if (scaleClicked) {
                m_currentGizmoOperation = ImGuizmo::SCALE;
                Rapture::RP_INFO("Gizmo operation set to Scale");
            }
            
            ImGui::EndChild();

    renderEntityGizmo();

    ImGui::End();
}

void ViewportPanel::renderEntityGizmo() {
    
    // Only proceed if we have valid data
    if (!m_selectedEntity) {
        return;
    }
    
    auto [transformComponent, bbComp] = m_selectedEntity->tryGetComponents<Rapture::TransformComponent, Rapture::BoundingBoxComponent>();
    if (!transformComponent || !bbComp) {
        return;
    }

    glm::mat4 originalTransform = transformComponent->transforms.getTransform();
    glm::mat4 transformMatrix = originalTransform;

    glm::vec3 centerOffset = glm::vec3(0.0f);
    if (bbComp) {
        centerOffset = bbComp->localBoundingBox.getCenter();
        // Create gizmo matrix: T * translate(C) - this puts the gizmo at the center of the bounding box in world space
        transformMatrix = originalTransform * glm::translate(glm::mat4(1.0f), centerOffset);
    }

    auto& sceneManager = Rapture::SceneManager::getInstance();
    auto scene = sceneManager.getActiveScene();
    if (!scene) {
        Rapture::RP_ERROR("ViewportPanel::renderEntityGizmo - No active scene found");
        return;
    }

    auto mainCamera = scene->getSettings().mainCamera;
    if (!mainCamera) {
        Rapture::RP_ERROR("ViewportPanel::renderEntityGizmo - No main camera found");
        return;
    }

    auto& camComp = mainCamera->getComponent<Rapture::CameraComponent>();
    auto viewMatrix = camComp.camera.getViewMatrix();
    auto projectionMatrix = camComp.camera.getProjectionMatrix();

    // Set up ImGuizmo
    ImGuizmo::SetOrthographic(false);  // Using perspective view
    ImGuizmo::SetDrawlist();
    
    // Set the ImGuizmo rect to match our viewport
    ImGuizmo::SetRect(
        m_viewportPosition.x, 
        m_viewportPosition.y, 
        m_viewportSize.x, 
        m_viewportSize.y
    );
    
    
    // Manipulate the transform
    ImGuizmo::Manipulate(
        glm::value_ptr(viewMatrix),      // View matrix (from camera callback)
        glm::value_ptr(projectionMatrix), // Projection matrix (from camera callback)
        m_currentGizmoOperation,           // Current operation (translate, rotate, scale)
        m_currentGizmoMode,                // Current mode (local or world)
        glm::value_ptr(transformMatrix),   // Transform matrix to manipulate
        nullptr,                           // Delta matrix (optional)
        nullptr                            // Snap values (optional)
    );
    

    // Check if gizmo is being used
    if (ImGuizmo::IsOver()) {
    }
    
    // If ImGuizmo changed the transform matrix, update the entity
    if (ImGuizmo::IsUsing()) {
        
        Rapture::TransformComponent* transformComponent = m_selectedEntity->tryGetComponent<Rapture::TransformComponent>();
        if (transformComponent) {

            glm::vec3 position, rotation, scale;
            ImGuizmo::DecomposeMatrixToComponents(
                glm::value_ptr(transformMatrix),
                glm::value_ptr(position),
                glm::value_ptr(rotation),
                glm::value_ptr(scale)
            );
        
            // If we have a bounding box, we need to subtract the center offset from the new position
            if (bbComp) {
                // Create a matrix with the new rotation and scale to transform the center offset
                glm::mat4 rotationScaleMatrix = glm::mat4(1.0f);
                rotationScaleMatrix = glm::rotate(rotationScaleMatrix, glm::radians(rotation.x), glm::vec3(1, 0, 0));
                rotationScaleMatrix = glm::rotate(rotationScaleMatrix, glm::radians(rotation.y), glm::vec3(0, 1, 0));
                rotationScaleMatrix = glm::rotate(rotationScaleMatrix, glm::radians(rotation.z), glm::vec3(0, 0, 1));
                rotationScaleMatrix = glm::scale(rotationScaleMatrix, scale);
                
                // Transform the center offset to world space with the new rotation and scale
                glm::vec3 worldCenterOffset = glm::vec3(rotationScaleMatrix * glm::vec4(centerOffset, 1.0f));
                
                // Subtract the transformed center offset from the gizmo position to get the actual object position
                position = position - worldCenterOffset;
            }

            // Update the transform component with new values using proper setters
            transformComponent->transforms.setTranslation(position);
            transformComponent->transforms.setRotation(glm::radians(rotation)); // Convert from degrees to radians
            transformComponent->transforms.setScale(scale);
            
            // Make sure to recalculate the transform matrix
            transformComponent->transforms.recalculateTransform();            
            // Update bounding box if it exists
            //if (auto* bb = selectedEntity->tryGetComponent<Rapture::BoundingBoxComponent>()) {
            //    bb->needsUpdate = true;
            //}
            
        }
    }
}

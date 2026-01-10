#include "ViewportPanel.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"

#include "Components/Components.h"

#include "Scenes/SceneManager.h"
#include "imguiPanels/IconsMaterialDesign.h"
#include "imguiPanels/modules/BetterPrimitives.h"
#include "imguiPanels/themes/imguiPanelStyle.h"

#include "Events/ApplicationEvents.h"
#include "Events/GameEvents.h"

#include <string>

ViewportPanel::ViewportPanel() : m_viewportSize(0, 0), m_lastViewportSize(0, 0)
{

    m_entitySelectedListenerId = Rapture::GameEvents::onEntitySelected().addListener(
        [this](std::shared_ptr<Rapture::Entity> entity) { m_selectedEntity = entity; });
}

ViewportPanel::~ViewportPanel()
{

    Rapture::GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId);
}

void ViewportPanel::renderSceneViewport(ImTextureID textureID)
{
    RAPTURE_PROFILE_FUNCTION();

    if (!m_isVisible) {
        return;
    }

    std::string title = "Viewport " + std::string(ICON_MD_WEB_ASSET);

    if (!BetterUi::BeginPanel(title.c_str())) {
        BetterUi::EndPanel();
        return;
    }

    BetterUi::BeginContent();

    // Render the topbar first
    renderTopbar();

    // After the topbar, get the actual viewport position and size
    m_viewportPosition = ImGui::GetCursorScreenPos();
    m_viewportSize = ImGui::GetContentRegionAvail();

    // Check for size changes and publish resize event if needed
    checkForSizeChange();

    ImGui::Image(textureID, m_viewportSize);

    // Calculate position for the gizmo controls overlay (relative to viewport image, not window)
    ImVec2 controlPos(m_viewportPosition.x + m_viewportSize.x - 180, // Right side of viewport
                      m_viewportPosition.y + 10                      // Below topbar with small padding
    );

    // Save the current cursor position
    ImVec2 origCursorPos = ImGui::GetCursorPos();

    // Begin overlay for gizmo controls
    ImGui::SetNextWindowPos(controlPos);
    ImGui::SetNextWindowBgAlpha(0.85f);                       // Less transparent background
    ImGui::BeginChild("GizmoControls", ImVec2(160, 60), true, // Wider panel
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

    bool isTranslate = m_currentGizmoOperation == Modules::Gizmo::Operation::TRANSLATE;
    bool isRotate = m_currentGizmoOperation == Modules::Gizmo::Operation::ROTATE;
    bool isScale = m_currentGizmoOperation == Modules::Gizmo::Operation::SCALE;

    // Get the background and hover colors from style
    ImVec4 defaultBgColor = ColorPalette::BACKGROUND_TERTIARY;
    ImVec4 selectedBgColor = ColorPalette::ACCENT_PRIMARY;
    ImVec4 textColor = ColorPalette::TEXT_NORMAL;
    ImVec4 hoverColor = ColorPalette::ACCENT_PRIMARY;
    ImVec4 accentColor = ColorPalette::ACCENT_PRIMARY;

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
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    // Draw 3 arrows for translate (X, Y, Z)
    float arrowSize = iconSize * 0.5f;
    // X arrow (red)
    drawList->AddLine(ImVec2(buttonCenter.x - arrowSize * 0.5f, buttonCenter.y), ImVec2(buttonCenter.x + arrowSize, buttonCenter.y),
                      IM_COL32(255, 50, 50, 255), 2.0f);
    drawList->AddTriangleFilled(ImVec2(buttonCenter.x + arrowSize, buttonCenter.y - arrowSize * 0.3f),
                                ImVec2(buttonCenter.x + arrowSize, buttonCenter.y + arrowSize * 0.3f),
                                ImVec2(buttonCenter.x + arrowSize * 1.5f, buttonCenter.y), IM_COL32(255, 50, 50, 255));

    // Y arrow (green)
    drawList->AddLine(ImVec2(buttonCenter.x, buttonCenter.y + arrowSize * 0.5f), ImVec2(buttonCenter.x, buttonCenter.y - arrowSize),
                      IM_COL32(50, 255, 50, 255), 2.0f);
    drawList->AddTriangleFilled(ImVec2(buttonCenter.x - arrowSize * 0.3f, buttonCenter.y - arrowSize),
                                ImVec2(buttonCenter.x + arrowSize * 0.3f, buttonCenter.y - arrowSize),
                                ImVec2(buttonCenter.x, buttonCenter.y - arrowSize * 1.5f), IM_COL32(50, 255, 50, 255));

    // Z arrow (blue, at 45 degrees)
    drawList->AddLine(ImVec2(buttonCenter.x - arrowSize * 0.35f, buttonCenter.y + arrowSize * 0.35f),
                      ImVec2(buttonCenter.x - arrowSize * 0.7f, buttonCenter.y + arrowSize * 0.7f), IM_COL32(50, 50, 255, 255),
                      2.0f);
    drawList->AddTriangleFilled(
        ImVec2(buttonCenter.x - arrowSize * 0.7f - arrowSize * 0.2f, buttonCenter.y + arrowSize * 0.7f),
        ImVec2(buttonCenter.x - arrowSize * 0.7f, buttonCenter.y + arrowSize * 0.7f + arrowSize * 0.2f),
        ImVec2(buttonCenter.x - arrowSize * 0.7f - arrowSize * 0.3f, buttonCenter.y + arrowSize * 0.7f + arrowSize * 0.3f),
        IM_COL32(50, 50, 255, 255));

    ImGui::PopStyleColor(2);

    if (translateClicked) {
        m_currentGizmoOperation = Modules::Gizmo::Operation::TRANSLATE;
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
    drawList->AddTriangleFilled(ImVec2(arrowX, arrowY),
                                ImVec2(arrowX + arrowTipSize * cosf(angle + 2.5f), arrowY + arrowTipSize * sinf(angle + 2.5f)),
                                ImVec2(arrowX + arrowTipSize * cosf(angle - 2.5f), arrowY + arrowTipSize * sinf(angle - 2.5f)),
                                IM_COL32(255, 255, 255, 200));

    ImGui::PopStyleColor(2);

    if (rotateClicked) {
        m_currentGizmoOperation = Modules::Gizmo::Operation::ROTATE;
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
    drawList->AddRect(ImVec2(buttonCenter.x - boxSize, buttonCenter.y - boxSize),
                      ImVec2(buttonCenter.x + boxSize, buttonCenter.y + boxSize), IM_COL32(255, 255, 255, 200), 0.0f, 0, 2.0f);

    // Draw corner handles
    float handleSize = 3.0f;
    // Top-left corner
    drawList->AddLine(ImVec2(buttonCenter.x - boxSize - handleSize, buttonCenter.y - boxSize),
                      ImVec2(buttonCenter.x - boxSize, buttonCenter.y - boxSize), IM_COL32(255, 255, 255, 200), 2.0f);
    drawList->AddLine(ImVec2(buttonCenter.x - boxSize, buttonCenter.y - boxSize - handleSize),
                      ImVec2(buttonCenter.x - boxSize, buttonCenter.y - boxSize), IM_COL32(255, 255, 255, 200), 2.0f);

    // Top-right corner
    drawList->AddLine(ImVec2(buttonCenter.x + boxSize + handleSize, buttonCenter.y - boxSize),
                      ImVec2(buttonCenter.x + boxSize, buttonCenter.y - boxSize), IM_COL32(255, 255, 255, 200), 2.0f);
    drawList->AddLine(ImVec2(buttonCenter.x + boxSize, buttonCenter.y - boxSize - handleSize),
                      ImVec2(buttonCenter.x + boxSize, buttonCenter.y - boxSize), IM_COL32(255, 255, 255, 200), 2.0f);

    // Bottom-left corner
    drawList->AddLine(ImVec2(buttonCenter.x - boxSize - handleSize, buttonCenter.y + boxSize),
                      ImVec2(buttonCenter.x - boxSize, buttonCenter.y + boxSize), IM_COL32(255, 255, 255, 200), 2.0f);
    drawList->AddLine(ImVec2(buttonCenter.x - boxSize, buttonCenter.y + boxSize + handleSize),
                      ImVec2(buttonCenter.x - boxSize, buttonCenter.y + boxSize), IM_COL32(255, 255, 255, 200), 2.0f);

    // Bottom-right corner
    drawList->AddLine(ImVec2(buttonCenter.x + boxSize + handleSize, buttonCenter.y + boxSize),
                      ImVec2(buttonCenter.x + boxSize, buttonCenter.y + boxSize), IM_COL32(255, 255, 255, 200), 2.0f);
    drawList->AddLine(ImVec2(buttonCenter.x + boxSize, buttonCenter.y + boxSize + handleSize),
                      ImVec2(buttonCenter.x + boxSize, buttonCenter.y + boxSize), IM_COL32(255, 255, 255, 200), 2.0f);

    ImGui::PopStyleColor(2);

    if (scaleClicked) {
        m_currentGizmoOperation = Modules::Gizmo::Operation::SCALE;
        Rapture::RP_INFO("Gizmo operation set to Scale");
    }

    ImGui::EndChild();

    // Render gizmo while still inside the content window so GetWindowDrawList() works
    renderEntityGizmo();

    BetterUi::EndContent();
    BetterUi::EndPanel();
}

void ViewportPanel::renderEntityGizmo()
{
    if (!m_selectedEntity) {
        m_previousSelectedEntity = nullptr;
        return;
    }

    if (m_selectedEntity != m_previousSelectedEntity) {
        m_gizmo.reset();
        m_previousSelectedEntity = m_selectedEntity;
    }

    auto [transformComponent, bbComp] =
        m_selectedEntity->tryGetComponents<Rapture::TransformComponent, Rapture::BoundingBoxComponent>();
    if (!transformComponent) {
        return;
    }

    auto &sceneManager = Rapture::SceneManager::getInstance();
    auto scene = sceneManager.getActiveScene();
    if (!scene) {
        return;
    }

    auto mainCamera = scene->getMainCamera();
    if (!mainCamera) {
        return;
    }

    auto &camComp = mainCamera.getComponent<Rapture::CameraComponent>();
    glm::mat4 viewMatrix = camComp.camera.getViewMatrix();
    glm::mat4 projectionMatrix = camComp.camera.getProjectionMatrix();

    glm::mat4 objectTransform = transformComponent->transforms.getTransform();
    glm::vec3 pivot = bbComp ? bbComp->localBoundingBox.getCenter() : glm::vec3(0.0f);

    ImDrawList *drawList = ImGui::GetWindowDrawList();

    Modules::Gizmo::Result result = m_gizmo.update(viewMatrix, projectionMatrix, objectTransform, pivot, m_currentGizmoOperation,
                                                   m_currentGizmoSpace, drawList, m_viewportPosition, m_viewportSize);

    if (result.active) {
        glm::vec3 position = transformComponent->transforms.getTranslation();
        glm::quat rotation = transformComponent->transforms.getRotationQuat(); // Get as quaternion
        glm::vec3 scale = transformComponent->transforms.getScale();

        position += result.deltaPosition;
        scale *= result.deltaScale;

        float rotationAngle = glm::length(result.deltaRotation);
        if (rotationAngle > 0.0001f) {
            glm::vec3 rotationAxis = result.deltaRotation / rotationAngle;
            glm::quat deltaQuat = glm::angleAxis(rotationAngle, rotationAxis);

            if (m_currentGizmoSpace == Modules::Gizmo::Space::WORLD) {
                rotation = deltaQuat * rotation; // World space (pre-multiply)
            } else {
                rotation = rotation * deltaQuat; // Local space (post-multiply)
            }
        }

        transformComponent->transforms.setTranslation(position);
        transformComponent->transforms.setRotation(rotation);
        transformComponent->transforms.setScale(scale);
        transformComponent->transforms.recalculateTransform();
    }
}

void ViewportPanel::renderTopbar()
{
    ImVec2 contentRegion = ImGui::GetContentRegionAvail();

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ColorPalette::BACKGROUND_SECONDARY);

    ImGui::BeginChild("ViewportTopbar", ImVec2(contentRegion.x, TOPBAR_HEIGHT), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Play/Pause button placeholder
    if (ImGui::Button(ICON_MD_PLAY_ARROW "##Play", ImVec2(28, 24))) {
        // TODO: Play scene
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Play");
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_MD_PAUSE "##Pause", ImVec2(28, 24))) {
        // TODO: Pause scene
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Pause");
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_MD_STOP "##Stop", ImVec2(28, 24))) {
        // TODO: Stop scene
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Stop");
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Add mesh dropdown placeholder
    if (ImGui::Button(ICON_MD_ADD_BOX " Add##AddMesh", ImVec2(0, 24))) {
        ImGui::OpenPopup("AddMeshPopup");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add mesh to scene");
    }

    if (ImGui::BeginPopup("AddMeshPopup")) {
        if (ImGui::MenuItem(ICON_MD_CROP_SQUARE " Cube")) {
            // TODO: Add cube
        }
        if (ImGui::MenuItem(ICON_MD_CIRCLE " Sphere")) {
            // TODO: Add sphere
        }
        if (ImGui::MenuItem(ICON_MD_CHANGE_HISTORY " Plane")) {
            // TODO: Add plane
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void ViewportPanel::checkForSizeChange()
{
    // Only publish event if size actually changed and is valid
    if (m_viewportSize.x > 0 && m_viewportSize.y > 0) {
        // Check if size changed significantly (more than 1 pixel to avoid float precision issues)
        bool sizeChanged =
            std::abs(m_viewportSize.x - m_lastViewportSize.x) > 1.0f || std::abs(m_viewportSize.y - m_lastViewportSize.y) > 1.0f;

        if (sizeChanged) {
            m_lastViewportSize = m_viewportSize;

            // Publish the viewport resize event
            Rapture::ApplicationEvents::onViewportResize().publish(static_cast<unsigned int>(m_viewportSize.x),
                                                                   static_cast<unsigned int>(m_viewportSize.y));
        }
    }
}

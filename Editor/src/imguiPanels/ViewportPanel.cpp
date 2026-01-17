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

    BetterUi::BeginContent(2.0f, 2.0f);

    renderTopbar();

    m_viewportPosition = ImGui::GetCursorScreenPos();
    m_viewportSize = ImGui::GetContentRegionAvail();

    checkForSizeChange();

    ImGui::Image(textureID, m_viewportSize);

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

void ViewportPanel::renderHotBar()
{

    ImGui::BeginGroup();

    if (ImGui::Button("Translate")) {
        m_currentGizmoOperation = Modules::Gizmo::Operation::TRANSLATE;
    }
    ImGui::SameLine();
    if (ImGui::Button("Rotate")) {
        m_currentGizmoOperation = Modules::Gizmo::Operation::ROTATE;
    }
    ImGui::SameLine();
    if (ImGui::Button("Scale")) {
        m_currentGizmoOperation = Modules::Gizmo::Operation::SCALE;
    }

    ImGui::SameLine();

    const char *spaceText = m_currentGizmoSpace == Modules::Gizmo::Space::WORLD ? "World" : "Local";
    if (ImGui::Button(spaceText)) {
        toggleGizmoSpace();
    }

    ImGui::EndGroup();
}

void ViewportPanel::renderGizmoControls() {}

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

    ImGui::SameLine();

    if (ImGui::Button(ICON_MD_PAUSE "##Pause", ImVec2(28, 24))) {
        // TODO: Pause scene
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_MD_STOP "##Stop", ImVec2(28, 24))) {
        // TODO: Stop scene
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    if (ImGui::Button("Add")) {
        ImVec2 pos = ImGui::GetItemRectMin();
        ImVec2 size = ImGui::GetItemRectSize();
        ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + size.y));
        ImGui::OpenPopup("AddPopup");
    }

    if (ImGui::BeginPopup("AddPopup")) {
        if (ImGui::BeginMenu("Primitive", true)) {

            if (ImGui::MenuItem("Cube")) {
            }
            if (ImGui::MenuItem("Sphere")) {
            }
            if (ImGui::MenuItem("Plane")) {
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Light", true)) {

            if (ImGui::MenuItem("Spot")) {
            }
            if (ImGui::MenuItem("Point")) {
            }
            if (ImGui::MenuItem("Sun")) {
            }
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    renderHotBar();
    ImGui::SameLine();
    renderGizmoControls();
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

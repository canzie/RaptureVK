#include "ViewportPanel.h"

#include "components/Components.h"
#include "events/GameEvents.h"
#include "logging/Log.h"
#include "scenes/SceneManager.h"

#include <components/common.h>
#include <components/extensions/ui_list_layout.h>

#include <glm/gtc/matrix_transform.hpp>

ViewportPanel::ViewportPanel(Amethyst::DockingLayer *dockingLayer) : m_dockingLayer(dockingLayer)
{
    auto root = std::make_unique<Amethyst::PanelLayer>();
    m_root = root.get();
    m_root->name = "Viewport";
    m_root->markDirty();

    m_header = m_root->add<Amethyst::Frame>();
    m_header->size = Amethyst::UDim2::fromScale(1.0f, 0.05f);
    m_header->backgroundColor = Amethyst::Color3(0.2f);
    m_header->markDirty();

    setupHeader();

    m_viewportImage = m_root->add<Amethyst::ImageLabel>();
    m_viewportImage->size = Amethyst::UDim2::fromScale(1.0f, 0.95f);
    m_viewportImage->position = Amethyst::UDim2::fromScale(0.0f, 0.05f);
    m_viewportImage->scaleType = Amethyst::ScaleType::STRETCH;
    m_viewportImage->zIndex = 100;
    m_viewportImage->cornerRadius = 2.0f;
    m_viewportImage->markDirty();

    m_gizmo = std::make_unique<Amethyst::Gizmo>(m_viewportImage);

    m_entitySelectedListenerId = Rapture::GameEvents::onEntitySelected().addListener(
        [this](std::shared_ptr<Rapture::Entity> entity) { m_selectedEntity = entity; });

    m_dockingLayer->dock(std::move(root), glm::vec2(0.0f));
}

ViewportPanel::~ViewportPanel()
{
    Rapture::GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId);

    if (m_dockingLayer && m_root) {
        m_dockingLayer->undock(m_root);
    }
}

void ViewportPanel::setupHeader()
{
    auto *layout = m_header->addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->innerPadding = Amethyst::UDim(0, 4);
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;

    m_translateBtn = m_header->add<Amethyst::TextButton>();
    m_translateBtn->text = "Translate";
    m_translateBtn->size = Amethyst::UDim2::fromOffset(80, 24);
    m_translateBtn->fontSize = 12.0f;
    m_translateBtn->textXAlignment = Amethyst::TextXAlignment::CENTER;
    m_translateBtn->textYAlignment = Amethyst::TextYAlignment::CENTER;
    m_translateBtn->markDirty();
    m_translateBtn->onMouseButton1ClickCb = [this]() {
        m_gizmoOperation = Amethyst::GizmoOperation::TRANSLATE;
        return Amethyst::EventResult::CONSUMED;
    };

    m_rotateBtn = m_header->add<Amethyst::TextButton>();
    m_rotateBtn->text = "Rotate";
    m_rotateBtn->size = Amethyst::UDim2::fromOffset(80, 24);
    m_rotateBtn->fontSize = 12.0f;
    m_rotateBtn->textXAlignment = Amethyst::TextXAlignment::CENTER;
    m_rotateBtn->textYAlignment = Amethyst::TextYAlignment::CENTER;
    m_rotateBtn->markDirty();
    m_rotateBtn->onMouseButton1ClickCb = [this]() {
        m_gizmoOperation = Amethyst::GizmoOperation::ROTATE;
        return Amethyst::EventResult::CONSUMED;
    };

    m_scaleBtn = m_header->add<Amethyst::TextButton>();
    m_scaleBtn->text = "Scale";
    m_scaleBtn->size = Amethyst::UDim2::fromOffset(80, 24);
    m_scaleBtn->fontSize = 12.0f;
    m_scaleBtn->textXAlignment = Amethyst::TextXAlignment::CENTER;
    m_scaleBtn->textYAlignment = Amethyst::TextYAlignment::CENTER;
    m_scaleBtn->markDirty();
    m_scaleBtn->onMouseButton1ClickCb = [this]() {
        m_gizmoOperation = Amethyst::GizmoOperation::SCALE;
        return Amethyst::EventResult::CONSUMED;
    };

    m_spaceBtn = m_header->add<Amethyst::TextButton>();
    m_spaceBtn->text = "World";
    m_spaceBtn->size = Amethyst::UDim2::fromOffset(80, 24);
    m_spaceBtn->fontSize = 12.0f;
    m_spaceBtn->textXAlignment = Amethyst::TextXAlignment::CENTER;
    m_spaceBtn->textYAlignment = Amethyst::TextYAlignment::CENTER;
    m_spaceBtn->markDirty();
    m_spaceBtn->onMouseButton1ClickCb = [this]() {
        if (m_gizmoSpace == Amethyst::GizmoSpace::WORLD) {
            m_gizmoSpace = Amethyst::GizmoSpace::LOCAL;
            m_spaceBtn->text = "Local";
        } else {
            m_gizmoSpace = Amethyst::GizmoSpace::WORLD;
            m_spaceBtn->text = "World";
        }
        m_spaceBtn->markDirty();
        return Amethyst::EventResult::CONSUMED;
    };
}

void ViewportPanel::setViewportImage(Amethyst::AmTextureId imageId)
{
    m_viewportImage->image = imageId;
    m_viewportImage->markDirty();
}

void ViewportPanel::onUpdate(float dt)
{
    updateGizmo();
}

void ViewportPanel::updateGizmo()
{
    if (!m_selectedEntity) {
        m_previousSelectedEntity = nullptr;
        return;
    }

    if (m_selectedEntity != m_previousSelectedEntity) {
        m_gizmo->reset();
        m_previousSelectedEntity = m_selectedEntity;
    }

    auto [transformComponent, bbComp] =
        m_selectedEntity->tryGetComponents<Rapture::TransformComponent, Rapture::BoundingBoxComponent>();
    if (!transformComponent) {
        return;
    }

    auto scene = Rapture::SceneManager::getInstance().getActiveScene();
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

    Amethyst::GizmoParams params;
    params.view = viewMatrix;
    params.projection = projectionMatrix;
    params.objectTransform = objectTransform;
    params.pivot = pivot;
    params.operation = m_gizmoOperation;
    params.space = m_gizmoSpace;

    Amethyst::GizmoResult result = m_gizmo->update(params);

    if (result.active) {
        glm::vec3 position = transformComponent->transforms.getTranslation();
        glm::quat rotation = transformComponent->transforms.getRotationQuat();
        glm::vec3 scale = transformComponent->transforms.getScale();

        position += result.deltaPosition;
        scale *= result.deltaScale;

        float rotationAngle = glm::length(result.deltaRotation);
        if (rotationAngle > 0.0001f) {
            glm::vec3 rotationAxis = result.deltaRotation / rotationAngle;
            glm::quat deltaQuat = glm::angleAxis(rotationAngle, rotationAxis);

            if (m_gizmoSpace == Amethyst::GizmoSpace::WORLD) {
                rotation = deltaQuat * rotation;
            } else {
                rotation = rotation * deltaQuat;
            }
        }

        transformComponent->transforms.setTranslation(position);
        transformComponent->transforms.setRotation(rotation);
        transformComponent->transforms.setScale(scale);
        transformComponent->transforms.recalculateTransform();
    }
}

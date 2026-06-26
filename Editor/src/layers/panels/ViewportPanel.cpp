#include "ViewportPanel.h"

#include "Icons.h"
#include "components/Components.h"
#include "components/systems/CameraController.h"
#include "events/GameEvents.h"
#include "layers/panels/components/tab_layouts.h"
#include "viewport/Viewport.h"
#include "window_context/Application.h"

#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>

#include <glm/gtc/matrix_transform.hpp>

static const Amethyst::UDim2 HEADER_BTN_SIZE = Amethyst::UDim2::fromOffset(80, 24);
static const Amethyst::TextStyleProperties HEADER_BTN_TEXT{
    .fontSize = 12.0f,
    .textXAlignment = Amethyst::TextXAlignment::CENTER,
    .textYAlignment = Amethyst::TextYAlignment::CENTER,
};

ViewportPanel::ViewportPanel(Amethyst::TabBar *tabBar)
{
    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) { m_root = nullptr; });
    m_root->name = "Viewport";
    m_root->addClass("background-secondary");
    m_root->setBaseProperties({.clipsDescendants = true});

    Amethyst::UIScope(*m_root)
        .frame(
            {
                .classes = {"background-tertiary"},
                .base = {.size = Amethyst::UDim2::fromScale(1.0f, 0.05f)},
            },
            [this](Amethyst::FrameScope &f) {
                m_header = &f.component;
                auto *layout = f.component.addExtension<Amethyst::UIListLayout>();
                layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                layout->innerPadding = Amethyst::UDim(0, 4);
                layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
                setupHeader(f);
            })
        .imageLabel(
            {
                .base =
                    {
                        .position = Amethyst::UDim2::fromScale(0.0f, 0.05f),
                        .size = Amethyst::UDim2::fromScale(1.0f, 0.95f),
                        .zIndex = 100,
                    },
                .style = {.cornerRadius = 2.0f},
                .image = {.scaleType = Amethyst::ImageScaleType::STRETCH},
            },
            [this](Amethyst::ImageLabelScope &img) {
                m_viewportImage = &img.component;
                m_viewportImageDestroyConn = m_viewportImage->onDestroy.connect(
                    [this](Amethyst::Instance *) { m_viewportImage = nullptr; });
                m_viewportImage->track(
                    m_viewportImage->onHoverChanged.connect([this](bool hovered) { m_viewportHovered = hovered; }));
            });

    m_gizmo = std::make_unique<Amethyst::Gizmo>(m_viewportImage);

    m_entitySelectedListenerId = Rapture::GameEvents::onEntitySelected().addListener(
        [this](std::shared_ptr<Rapture::Entity> entity) { m_selectedEntity = entity; });

    tabBar->addTab(std::move(root), iconTabLayout("Viewport", Icons::SVG_VIEWPORT));
}

ViewportPanel::~ViewportPanel()
{
    Rapture::GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId);
    if (m_root != nullptr && m_root->parent != nullptr) {
        if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
            tabBar->removeTab(m_root);
        }
    }
}

void ViewportPanel::setupHeader(Amethyst::FrameScope &f)
{
    f.textButton(
        {
            .base = {.size = HEADER_BTN_SIZE},
            .text = HEADER_BTN_TEXT,
            .label = "Translate",
        },
        [this](Amethyst::TextButtonScope &b) {
            m_translateBtn = &b.component;
            b.component.onMouseButton1ClickCb = [this]() {
                m_gizmoOperation = Amethyst::GizmoOperation::TRANSLATE;
                return Amethyst::EventResult::CONSUMED;
            };
        });
    f.textButton(
        {
            .base = {.size = HEADER_BTN_SIZE},
            .text = HEADER_BTN_TEXT,
            .label = "Rotate",
        },
        [this](Amethyst::TextButtonScope &b) {
            m_rotateBtn = &b.component;
            b.component.onMouseButton1ClickCb = [this]() {
                m_gizmoOperation = Amethyst::GizmoOperation::ROTATE;
                return Amethyst::EventResult::CONSUMED;
            };
        });
    f.textButton(
        {
            .base = {.size = HEADER_BTN_SIZE},
            .text = HEADER_BTN_TEXT,
            .label = "Scale",
        },
        [this](Amethyst::TextButtonScope &b) {
            m_scaleBtn = &b.component;
            b.component.onMouseButton1ClickCb = [this]() {
                m_gizmoOperation = Amethyst::GizmoOperation::SCALE;
                return Amethyst::EventResult::CONSUMED;
            };
        });
    f.textButton(
        {
            .base = {.size = HEADER_BTN_SIZE},
            .text = HEADER_BTN_TEXT,
            .label = "World",
        },
        [this](Amethyst::TextButtonScope &b) {
            m_spaceBtn = &b.component;
            b.component.onMouseButton1ClickCb = [this]() {
                if (m_gizmoSpace == Amethyst::GizmoSpace::WORLD) {
                    m_gizmoSpace = Amethyst::GizmoSpace::LOCAL;
                    m_spaceBtn->setText("Local");
                } else {
                    m_gizmoSpace = Amethyst::GizmoSpace::WORLD;
                    m_spaceBtn->setText("World");
                }
                return Amethyst::EventResult::CONSUMED;
            };
        });
    f.textButton(
        {
            .base = {.size = HEADER_BTN_SIZE},
            .text = HEADER_BTN_TEXT,
            .label = "Orbit",
        },
        [this](Amethyst::TextButtonScope &b) {
            m_cameraModeBtn = &b.component;
            b.component.onMouseButton1ClickCb = [this]() {
                auto *controller = cameraController();
                if (controller == nullptr) {
                    return Amethyst::EventResult::CONSUMED;
                }
                if (controller->getMode() == Rapture::CameraControlMode::FLY) {
                    controller->setMode(Rapture::CameraControlMode::ORBIT);
                } else {
                    controller->setMode(Rapture::CameraControlMode::FLY);
                }
                return Amethyst::EventResult::CONSUMED;
            };
        });
}

Rapture::CameraController *ViewportPanel::cameraController() const
{
    auto *viewport = Rapture::Application::getInstance().getViewportManager().getPrimaryViewport();
    if (viewport == nullptr) {
        return nullptr;
    }
    return viewport->editorBinding().controller;
}

void ViewportPanel::syncCameraModeButton()
{
    auto *controller = cameraController();
    if (controller == nullptr || m_cameraModeBtn == nullptr) {
        return;
    }
    m_cameraModeBtn->setText(controller->getMode() == Rapture::CameraControlMode::FLY ? "Fly" : "Orbit");
}

void ViewportPanel::setViewportImage(Amethyst::AmTextureId imageId)
{
    if (m_viewportImage != nullptr) {
        m_viewportImage->setImage(imageId);
    }
}

void ViewportPanel::onUpdate(float dt)
{
    if (m_root == nullptr) {
        return;
    }

    updateGizmo();
    syncCameraModeButton();

    auto *viewport = Rapture::Application::getInstance().getViewportManager().getPrimaryViewport();
    if (viewport != nullptr) {
        viewport->editorBinding().hovered = m_viewportHovered;
    }
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

    auto scene = Rapture::Application::getInstance().getProject().getActiveScene();
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

        glm::vec3 deltaPosition(result.deltaPosition.x, result.deltaPosition.y, result.deltaPosition.z);
        glm::vec3 deltaScale(result.deltaScale.x, result.deltaScale.y, result.deltaScale.z);
        glm::vec3 deltaRotation(result.deltaRotation.x, result.deltaRotation.y, result.deltaRotation.z);

        position += deltaPosition;
        scale *= deltaScale;

        float rotationAngle = glm::length(deltaRotation);
        if (rotationAngle > 0.0001f) {
            glm::vec3 rotationAxis = deltaRotation / rotationAngle;
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

        m_selectedEntity->markDirty();
    }
}

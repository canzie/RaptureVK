#include "ViewportPanel.h"

#include "Icons.h"
#include "components/Components.h"
#include "components/systems/CameraController.h"
#include "events/GameEvents.h"
#include "layers/panels/components/context_menus.h"
#include "layers/panels/components/tab_layouts.h"
#include "render_targets/SceneRenderTarget.h"
#include "utils/rp_assert.h"
#include "viewport/Viewport.h"

#include <components/checkbox.h>
#include <components/common.h>
#include <components/extensions/ui_list_layout.h>
#include <components/input_events.h>
#include <components/ui_scope.h>

#include "logging/Log.h"
#include "renderer/RenderSettings.h"

#include <algorithm>
#include <climits>
#include <glm/gtc/matrix_transform.hpp>
#include <math/math.h>

static constexpr float VIEWPORT_RESIZE_DEBOUNCE = 0.1f;
static constexpr float VIEWPORT_PADDING = 6.0f;

static const Amethyst::UDim2 HEADER_BTN_SIZE = Amethyst::UDim2::fromOffset(80, 24);
static const Amethyst::TextStylePropertiesArgs HEADER_BTN_TEXT{
    .fontSize = 12.0f,
    .textXAlignment = Amethyst::TextXAlignment::CENTER,
    .textYAlignment = Amethyst::TextYAlignment::CENTER,
};

static void s_showGrownMenu(Amethyst::ContextMenu *menu, Amethyst::UIObject *anchor, Amethyst::Frame *boundsRoot)
{
    if (menu == nullptr || anchor == nullptr || boundsRoot == nullptr) {
        return;
    }
    float anchorBottom = anchor->absolutePosition.y + anchor->absoluteSize.y;
    float boundsBottom = boundsRoot->absolutePosition.y + boundsRoot->absoluteSize.y;
    menu->maxContentHeight = std::max(0.0f, boundsBottom - anchorBottom);
    menu->show(anchor);
}

ViewportPanel::ViewportPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context) : Panel("Viewport", context)
{
    m_viewport = context.viewport;
    if (m_viewport != nullptr) {
        bool alreadyDisplayed = m_viewport->editorBinding().displayed;
        RP_ASSERT(!alreadyDisplayed, "viewport is already displayed by another panel");
        if (alreadyDisplayed) {
            m_viewport = nullptr;
        } else {
            m_viewport->editorBinding().displayed = true;
            if (context.scene != nullptr) {
                m_viewport->setScene(context.scene);
            }
        }
    }

    m_gizmoSpaceGroup.value = static_cast<int32_t>(m_gizmoSpace);
    m_lightingModeGroup.value = VLM_LIT;

    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) { m_root = nullptr; });
    m_root->addClass("panel");
    m_root->setBaseProperties({.clipsDescendants = true, .padding = Amethyst::UDim4::fromOffset(VIEWPORT_PADDING)});

    Amethyst::UIScope(*m_root).imageLabel(
        {
            .base =
                {
                    .padding = Amethyst::UDim4::fromOffset(8.0f),
                    .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
                    .zIndex = 100,
                },
            .style = {.cornerRadius = 2.0f},
            .image = {.scaleType = Amethyst::ImageScaleType::STRETCH},
        },
        [this](Amethyst::ImageLabelScope &img) {
            m_viewportImage = &img.component;
            m_viewportImage->propagate(Amethyst::INTERACTION_CATEGORY_ALL);
            m_viewportImageDestroyConn =
                m_viewportImage->onDestroy.connect([this](Amethyst::Instance *) { m_viewportImage = nullptr; });
            m_viewportImage->track(m_viewportImage->onHoverChanged.connect([this](bool hovered) { m_viewportHovered = hovered; }));
        });

    m_gizmo = std::make_unique<Amethyst::Gizmo>(m_viewportImage);

    setupOverlayButtons();
    buildTransformMenu();
    buildRenderMenu();

    m_entitySelectedListenerId = Rapture::GameEvents::onEntitySelected().addListener(
        [this](std::shared_ptr<Rapture::Entity> entity) { m_selectedEntity = entity; });

    m_entityDeselectedListenerId = Rapture::GameEvents::onEntityDeselected().addListener([this](Rapture::Entity entity) {
        if (m_selectedEntity != nullptr && *m_selectedEntity == entity) {
            m_selectedEntity = nullptr;
            m_gizmo->reset();
        }
    });

    icon = Icons::SVG_VIEWPORT;
    attach(tabBar, std::move(root));
}

ViewportPanel::~ViewportPanel()
{
    for (auto &slot : m_slotImages) {
        if (slot.id.isValid() && m_services.unregisterTexture) {
            m_services.unregisterTexture(slot.id);
        }
    }
    if (m_viewport != nullptr) {
        m_viewport->editorBinding().displayed = false;
    }

    Rapture::GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId);
    Rapture::GameEvents::onEntityDeselected().removeListener(m_entityDeselectedListenerId);
    if (m_root != nullptr && m_root->parent != nullptr) {
        if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
            tabBar->removeTab(m_root);
        }
    }
}

void ViewportPanel::setupOverlayButtons()
{
    static const float BTN_GAP = 4.0f;

    Amethyst::UIScope(*m_viewportImage)
        .textButton(
            {
                .classes = {"viewport-overlay-button"},
                .base =
                    {
                        .position = Amethyst::UDim2::fromScale(0.0f, 0.0f),
                        .size = HEADER_BTN_SIZE,
                        .zIndex = 101,
                    },
                .text = HEADER_BTN_TEXT,
                .label = "Transform",
            },
            [this](Amethyst::TextButtonScope &b) {
                m_transformMenuBtn = &b.component;
                b.component.onMouseButton1ClickCb = [this]() {
                    s_showGrownMenu(m_transformMenu, m_transformMenuBtn, m_root);
                    return Amethyst::EventResult::CONSUMED;
                };
            })
        .textButton(
            {
                .classes = {"viewport-overlay-button"},
                .base =
                    {
                        .position = Amethyst::UDim2::fromOffset(HEADER_BTN_SIZE.offset.x + BTN_GAP, 0.0f),
                        .size = HEADER_BTN_SIZE,
                        .zIndex = 101,
                    },
                .text = HEADER_BTN_TEXT,
                .label = "Render",
            },
            [this](Amethyst::TextButtonScope &b) {
                m_renderMenuBtn = &b.component;
                b.component.onMouseButton1ClickCb = [this]() {
                    s_showGrownMenu(m_renderMenu, m_renderMenuBtn, m_root);
                    return Amethyst::EventResult::CONSUMED;
                };
            });
}

void ViewportPanel::buildTransformMenu()
{
    m_transformMenu = m_root->add<Amethyst::ContextMenu>();
    m_transformMenu->addClass("viewport-context-menu");
    m_transformMenu->popupWidth *= 1.3f;
    m_transformMenu->maxVisibleItems = INT_MAX;
    m_transformMenu->setRowFactories({
        .separator = [] { return std::make_unique<ViewportContextMenuSIV>(); },
        .radio = [] { return std::make_unique<ViewportContextMenuRIV>(); },
    });

    m_gizmoOpGroupConn = m_gizmoOpGroup.onChanged.connect(
        [this]() { m_gizmoOperation = static_cast<Amethyst::GizmoOperation>(m_gizmoOpGroup.value); });
    m_gizmoSpaceGroupConn = m_gizmoSpaceGroup.onChanged.connect(
        [this]() { m_gizmoSpace = static_cast<Amethyst::GizmoSpace>(m_gizmoSpaceGroup.value); });
    m_cameraModeGroupConn = m_cameraModeGroup.onChanged.connect([this]() {
        auto *controller = cameraController();
        if (controller != nullptr) {
            controller->setMode(m_cameraModeGroup.value == static_cast<int32_t>(Rapture::CameraControlMode::ORBIT)
                                    ? Rapture::CameraControlMode::ORBIT
                                    : Rapture::CameraControlMode::FLY);
        }
    });

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
    items.push_back(
        ViewportContextMenuRID::create("Translate", &m_gizmoOpGroup, static_cast<int32_t>(Amethyst::GizmoOperation::TRANSLATE)));
    items.push_back(
        ViewportContextMenuRID::create("Rotate", &m_gizmoOpGroup, static_cast<int32_t>(Amethyst::GizmoOperation::ROTATE)));
    items.push_back(
        ViewportContextMenuRID::create("Scale", &m_gizmoOpGroup, static_cast<int32_t>(Amethyst::GizmoOperation::SCALE)));
    items.push_back(ViewportContextMenuSID::create("Space"));
    items.push_back(ViewportContextMenuRID::create("World", &m_gizmoSpaceGroup, static_cast<int32_t>(Amethyst::GizmoSpace::WORLD)));
    items.push_back(ViewportContextMenuRID::create("Local", &m_gizmoSpaceGroup, static_cast<int32_t>(Amethyst::GizmoSpace::LOCAL)));
    items.push_back(ViewportContextMenuSID::create("Camera"));
    items.push_back(
        ViewportContextMenuRID::create("Orbit", &m_cameraModeGroup, static_cast<int32_t>(Rapture::CameraControlMode::ORBIT)));
    items.push_back(
        ViewportContextMenuRID::create("Fly", &m_cameraModeGroup, static_cast<int32_t>(Rapture::CameraControlMode::FLY)));
    m_transformMenu->setItems(std::move(items));
}

void ViewportPanel::buildRenderMenu()
{
    m_renderMenu = m_root->add<Amethyst::ContextMenu>();
    m_renderMenu->addClass("viewport-context-menu");
    m_renderMenu->popupWidth *= 1.3f;
    m_renderMenu->maxVisibleItems = INT_MAX;
    m_renderMenu->setRowFactories({.toggle = [] { return std::make_unique<ViewportContextMenuTIV>(); },
                                   .radio = [] { return std::make_unique<ViewportContextMenuRIV>(); }});

    m_lightingModeGroupConn = m_lightingModeGroup.onChanged.connect(
        [this]() { applyLightingMode(static_cast<ViewportLightingMode>(m_lightingModeGroup.value)); });

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
    items.push_back(ViewportContextMenuRID::create("Lit", &m_lightingModeGroup, VLM_LIT));
    items.push_back(ViewportContextMenuRID::create("Direct Lighting", &m_lightingModeGroup, VLM_DIRECT_LIGHTING));
    items.push_back(ViewportContextMenuRID::create("Indirect Lighting", &m_lightingModeGroup, VLM_INDIRECT_LIGHTING));
    items.push_back(ViewportContextMenuRID::create("Raw Irradiance(no albedo)", &m_lightingModeGroup, VLM_RAW_IRRADIANCE));
    items.push_back(ViewportContextMenuRID::create("Show Normals", &m_lightingModeGroup, VLM_NORMALS));
    items.push_back(ViewportContextMenuRID::create("Show Motion Vectors", &m_lightingModeGroup, VLM_MOTION));
    items.push_back(ViewportContextMenuRID::create("Show Reflection Hits", &m_lightingModeGroup, VLM_SSSR_HIT));
    items.push_back(ViewportContextMenuRID::create("Show Reflections (Resolved)", &m_lightingModeGroup, VLM_SSSR_RESOLVED));
    items.push_back(
        ViewportContextMenuRID::create("Show Reflections (Accumulated)", &m_lightingModeGroup, VLM_SSSR_ACCUMULATED));
    items.push_back(
        ViewportContextMenuRID::create("Show Reflection Confidence", &m_lightingModeGroup, VLM_SSSR_CONFIDENCE));
    items.push_back(
        ViewportContextMenuRID::create("Show Ambient Occlusion", &m_lightingModeGroup, VLM_AMBIENT_OCCLUSION));
    items.push_back(ViewportContextMenuRID::create("Show Bent Normals", &m_lightingModeGroup, VLM_BENT_NORMALS));

    auto reflectionsToggle = ViewportContextMenuTID::create("Screen-Space Reflections", [this](bool on) {
        if (m_viewport != nullptr) {
            m_viewport->renderSettings().setFlag(Rapture::RENDER_USE_SCREEN_SPACE_REFLECTIONS, on);
        }
    });
    reflectionsToggle->as<ViewportContextMenuTID>().value = true;
    items.push_back(std::move(reflectionsToggle));

    auto occlusionToggle = ViewportContextMenuTID::create("Ambient Occlusion", [this](bool on) {
        if (m_viewport != nullptr) {
            m_viewport->renderSettings().setFlag(Rapture::RENDER_USE_AMBIENT_OCCLUSION, on);
        }
    });
    occlusionToggle->as<ViewportContextMenuTID>().value = true;
    items.push_back(std::move(occlusionToggle));

    m_renderMenu->setItems(std::move(items));
}

void ViewportPanel::applyLightingMode(ViewportLightingMode mode)
{
    auto *viewport = m_viewport;
    if (viewport == nullptr) {
        return;
    }
    auto &settings = viewport->renderSettings();

    settings.setFlag(Rapture::RENDER_SHOW_NORMALS, false);
    settings.setFlag(Rapture::RENDER_SHOW_MOTION, false);
    settings.setFlag(Rapture::RENDER_SHOW_SSSR_HIT, false);
    settings.setFlag(Rapture::RENDER_SHOW_SSSR_RESOLVED, false);
    settings.setFlag(Rapture::RENDER_SHOW_SSSR_ACCUMULATED, false);
    settings.setFlag(Rapture::RENDER_SHOW_SSSR_CONFIDENCE, false);
    settings.setFlag(Rapture::RENDER_SHOW_AMBIENT_OCCLUSION, false);
    settings.setFlag(Rapture::RENDER_SHOW_BENT_NORMALS, false);
    switch (mode) {
    case VLM_DIRECT_LIGHTING:
        settings.setFlag(Rapture::RENDER_SHOW_DIRECT, true);
        settings.setFlag(Rapture::RENDER_SHOW_INDIRECT, false);
        break;
    case VLM_INDIRECT_LIGHTING:
        settings.setFlag(Rapture::RENDER_SHOW_DIRECT, false);
        settings.setFlag(Rapture::RENDER_SHOW_INDIRECT, true);
        settings.setFlag(Rapture::RENDER_USE_GLOBAL_ILLUMINATION, true);
        settings.setFlag(Rapture::RENDER_MODULATE_INDIRECT, true);
        break;
    case VLM_RAW_IRRADIANCE:
        settings.setFlag(Rapture::RENDER_SHOW_DIRECT, false);
        settings.setFlag(Rapture::RENDER_SHOW_INDIRECT, true);
        settings.setFlag(Rapture::RENDER_USE_GLOBAL_ILLUMINATION, true);
        settings.setFlag(Rapture::RENDER_MODULATE_INDIRECT, false);
        break;
    case VLM_NORMALS:
        settings.setFlag(Rapture::RENDER_SHOW_NORMALS, true);
        break;
    case VLM_MOTION:
        settings.setFlag(Rapture::RENDER_SHOW_MOTION, true);
        break;
    case VLM_SSSR_HIT:
        settings.setFlag(Rapture::RENDER_SHOW_SSSR_HIT, true);
        break;
    case VLM_SSSR_RESOLVED:
        settings.setFlag(Rapture::RENDER_SHOW_SSSR_RESOLVED, true);
        break;
    case VLM_SSSR_ACCUMULATED:
        settings.setFlag(Rapture::RENDER_SHOW_SSSR_ACCUMULATED, true);
        break;
    case VLM_SSSR_CONFIDENCE:
        settings.setFlag(Rapture::RENDER_SHOW_SSSR_CONFIDENCE, true);
        break;
    case VLM_AMBIENT_OCCLUSION:
        settings.setFlag(Rapture::RENDER_SHOW_AMBIENT_OCCLUSION, true);
        break;
    case VLM_BENT_NORMALS:
        settings.setFlag(Rapture::RENDER_SHOW_BENT_NORMALS, true);
        break;
    case VLM_LIT:
    default:
        settings.setFlag(Rapture::RENDER_SHOW_DIRECT, true);
        settings.setFlag(Rapture::RENDER_SHOW_INDIRECT, true);
        settings.setFlag(Rapture::RENDER_USE_GLOBAL_ILLUMINATION, true);
        settings.setFlag(Rapture::RENDER_MODULATE_INDIRECT, true);
        break;
    }
}

Rapture::CameraController *ViewportPanel::cameraController() const
{
    auto *viewport = m_viewport;
    if (viewport == nullptr) {
        return nullptr;
    }
    return viewport->editorBinding().controller;
}

void ViewportPanel::setViewportImage(Amethyst::AmTextureId imageId)
{
    if (m_viewportImage != nullptr) {
        m_viewportImage->setImage(imageId);
    }
}

void ViewportPanel::updateViewportImage()
{
    if (m_viewport == nullptr) {
        return;
    }
    auto *target = m_viewport->getSceneRenderTarget();
    if (target == nullptr) {
        return;
    }
    uint32_t slot = m_viewport->getLastRenderedFrameIndex();
    if (slot == UINT32_MAX) {
        return;
    }
    if (slot >= m_slotImages.size()) {
        m_slotImages.resize(slot + 1);
    }

    auto texture = target->getTexture(slot);
    if (texture == nullptr) {
        return;
    }

    SlotImage &cached = m_slotImages[slot];
    if (texture.get() != cached.texture) {
        if (cached.id.isValid() && m_services.unregisterTexture) {
            m_services.unregisterTexture(cached.id);
        }
        if (m_services.registerTexture) {
            cached.id = m_services.registerTexture(texture.get());
        }
        cached.texture = texture.get();
    }

    if (cached.id.isValid()) {
        setViewportImage(cached.id);
    }
}

void ViewportPanel::onUpdate(float dt)
{
    if (m_root == nullptr) {
        return;
    }

    updateGizmo();

    auto *controller = cameraController();
    if (controller != nullptr) {
        m_cameraModeGroup.value = static_cast<int32_t>(controller->getMode());
    }

    if (m_viewport != nullptr) {
        m_viewport->editorBinding().hovered = m_viewportHovered;
    }
    updateViewportImage();

    auto *viewport = m_viewport;
    Amethyst::vec2 size = m_viewportImage->absoluteContentSize;
    if (size.x > 0.0f && size.y > 0.0f) {
        if (size != m_pendingViewportSize) {
            m_pendingViewportSize = size;
            m_resizeStableTime = 0.0f;
            m_resizePending = true;
        } else if (m_resizePending) {
            m_resizeStableTime += dt;
            if (m_resizeStableTime >= VIEWPORT_RESIZE_DEBOUNCE) {
                m_resizePending = false;
                if (size != m_lastViewportSize) {
                    m_lastViewportSize = size;
                    if (viewport != nullptr) {
                        viewport->resize(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));
                    }
                }
            }
        }
    }
}

void ViewportPanel::updateGizmo()
{
    if (m_selectedEntity != nullptr && !m_selectedEntity->isValid()) {
        m_selectedEntity = nullptr;
        m_gizmo->reset();
    }

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

    if (m_viewport == nullptr) {
        return;
    }
    auto *scene = m_viewport->getScene();
    if (scene == nullptr) {
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

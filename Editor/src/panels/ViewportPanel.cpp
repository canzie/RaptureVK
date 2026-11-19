#include "ViewportPanel.h"

#include "EntitySelection.h"
#include "Icons.h"
#include "scene/EditorCamera.h"
#include "ui/controls/IconLabelButton.h"
#include "ui/menus/ContextMenuItems.h"
#include <rapture/gpu/render_targets/SceneRenderTarget.h>
#include <rapture/renderer/viewport/Viewport.h>
#include <rapture/scene/World.h>
#include <rapture/scene/components/Components.h>
#include <rapture/scene/instances/Node3D.h>
#include <rapture/scene/instances/controllers/CameraController.h>

#include <components/checkbox.h>
#include <components/common.h>
#include <components/extensions/ui_list_layout.h>
#include <components/input_events.h>
#include <components/ui_scope.h>

#include <rapture/renderer/RenderSettings.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include <math/math.h>
#include <optional>
#include <span>

static constexpr float VIEWPORT_RESIZE_DEBOUNCE = 0.1f;
static constexpr float VIEWPORT_PADDING = 6.0f;

static constexpr std::string_view VIEWPORT_IMAGE_CLASS = "viewport-image";
static constexpr std::string_view VIEWPORT_IMAGE_PLAYING_CLASS = "viewport-image-playing";

// Odd so the cursor sits on a centre pixel. The aperture is the click tolerance, and it costs only
// the pixels it covers, so widening it is cheap
static constexpr uint32_t PICK_APERTURE = 11;

/**
 * @brief The entity nearest the cursor within a queried region
 *
 * An exact hit under the cursor beats a near miss because it is nearer in pixels, so tolerance and
 * pixel exactness do not compete. Only the front hit of a pixel is considered, since a first click
 * selects what is visible.
 *
 * @param result The region's hits
 * @param cursorX Cursor x within the region
 * @param cursorY Cursor y within the region
 * @return What was drawn there reports itself as, empty where the region was empty
 */
static std::optional<uint64_t> s_nearestToCursor(const Rapture::SceneQueryResult &result, uint32_t cursorX, uint32_t cursorY)
{
    std::optional<uint64_t> best;
    int32_t bestDistance = INT32_MAX;
    float bestDepth = 0.0f;

    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            std::span<const Rapture::SceneQueryHit> hits = result.at(x, y);
            if (hits.empty()) {
                continue;
            }

            const int32_t distance = std::max(std::abs(static_cast<int32_t>(x) - static_cast<int32_t>(cursorX)),
                                              std::abs(static_cast<int32_t>(y) - static_cast<int32_t>(cursorY)));

            if (distance > bestDistance || (distance == bestDistance && hits[0].depth >= bestDepth)) {
                continue;
            }

            best = hits[0].userData;
            bestDistance = distance;
            bestDepth = hits[0].depth;
        }
    }

    return best;
}

static constexpr float VIEWPORT_TOOLBAR_HEIGHT = 30.0f;
static constexpr float VIEWPORT_TOOLBAR_PADDING = 2.0f;
static constexpr float VIEWPORT_TOOLBAR_GAP = 2.0f;
static constexpr std::string_view VIEWPORT_TOOLBAR_CLASS = "viewport-toolbar";
static constexpr std::string_view VIEWPORT_TOOLBAR_BUTTON_CLASS = "viewport-toolbar-button";

static constexpr std::array<std::string_view, VLM_COUNT> VIEWPORT_LIGHTING_MODE_LABELS = {
    "Lit",
    "Raw Irradiance",
    "Indirect Lighting",
    "Direct Lighting",
    "Shadow Cascades",
    "Normals",
    "Motion Vectors",
    "Ambient Occlusion",
    "Metallic",
    "Roughness",
    "AO",
    "Albedo",
    "Material Id",
    "Depth",
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

ViewportPanel::ViewportPanel(const WorkspaceContext &context) : Panel("Viewport", context)
{
    m_icon = Icons::SVG_VIEWPORT;

    m_viewport = context.viewport;
    if (m_viewport == nullptr) {
        return;
    }

    if (context.scene != nullptr) {
        m_viewport->setScene(context.scene);
    }

    // nothing is looking at it until its tab comes to the front
    m_viewport->setActive(false);
}

void ViewportPanel::onShown()
{
    if (m_viewport != nullptr) {
        m_viewport->setActive(true);
    }
}

void ViewportPanel::onHidden()
{
    if (m_viewport != nullptr) {
        m_viewport->setActive(false);
    }
}

void ViewportPanel::build(Amethyst::UIScope &scope)
{
    m_gizmoSpaceGroup.value = static_cast<int32_t>(m_gizmoSpace);
    m_lightingModeGroup.value = VLM_LIT;

    m_root->setBaseProperties({.clipsDescendants = true, .padding = Amethyst::UDim4::fromOffset(VIEWPORT_PADDING)});

    scope.imageLabel(
        {
            .classes = {std::string(VIEWPORT_IMAGE_CLASS)},
            .base =
                {
                    .padding = Amethyst::UDim4::fromOffset(8.0f),
                    .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
                    .zIndex = 100,
                },
            .image = {.scaleType = Amethyst::ImageScaleType::STRETCH},
        },
        [this](Amethyst::ImageLabelScope &img) {
            m_viewportImage = &img.component;
            m_viewportImage->propagate(Amethyst::INTERACTION_CATEGORY_ALL);
            m_viewportImageDestroyConn =
                m_viewportImage->onDestroy.connect([this](Amethyst::Instance *) { m_viewportImage = nullptr; });
            m_viewportImage->track(m_viewportImage->onHoverChanged.connect([this](bool hovered) { m_viewportHovered = hovered; }));
            m_viewportImage->track(
                m_viewportImage->onInputBeganCb.connect([this](const Amethyst::InputObject &input) { onViewportPressed(input); }));
            m_viewportImage->track(m_viewportImage->onInputChangedCb.connect(
                [this](const Amethyst::InputObject &input) { onViewportCursorMoved(input); }));
            m_viewportImage->track(m_viewportImage->onInputEndedCb.connect(
                [this](const Amethyst::InputObject &input) { onViewportMouseReleased(input); }));
        });

    auto *gizmoContainer = m_viewportImage->add<Amethyst::Container>();
    gizmoContainer->setBaseProperties({.interactable = false, .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
    m_transformGizmo = std::make_unique<gizmo::TransformGizmo>(gizmoContainer);

    setupOverlayButtons();
    buildTransformMenu();
    buildRenderMenu();

    if (m_selection != nullptr) {
        m_selectionChangedConn = m_selection->onChanged.connect([this](Rapture::ecs::EntityAccessor entity) {
            m_selectedEntity = entity;
            if (!m_selectedEntity.isValid()) {
                m_transformGizmo->reset();
            }
        });
    }
}

void ViewportPanel::releaseSlotImages()
{
    UiTextures *textures = m_context.editor->textures;
    for (auto &slot : m_slotImages) {
        if (slot.id.isValid()) {
            textures->unregisterTexture(slot.id);
        }
    }

    m_slotImages.clear();
}

ViewportPanel::~ViewportPanel()
{
    releaseSlotImages();
    if (m_viewport != nullptr) {
        m_viewport->setActive(false);
    }
}

void ViewportPanel::setupOverlayButtons()
{
    Amethyst::UIScope(*m_viewportImage)
        .frame(
            {
                .classes = {std::string(VIEWPORT_TOOLBAR_CLASS)},
                .base =
                    {
                        .padding = Amethyst::UDim4::fromOffset(VIEWPORT_TOOLBAR_PADDING),
                        .zIndex = 101,
                    },
            },
            [this](Amethyst::FrameScope &toolbar) {
                m_viewportToolbar = &toolbar.component;

                auto *layout = m_viewportToolbar->addExtension<Amethyst::UIListLayout>();
                layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
                layout->innerPadding = Amethyst::UDim::fromOffset(VIEWPORT_TOOLBAR_GAP);
            });

    const std::vector<std::string> buttonClasses = {std::string(VIEWPORT_TOOLBAR_BUTTON_CLASS)};

    m_transformMenuBtn = m_viewportToolbar->add<IconLabelButton>(Icons::SVG_TRANSFORM, "Transform", buttonClasses);
    m_transformMenuBtn->setBaseProperties({.layoutOrder = 0});
    m_transformMenuBtn->track(m_transformMenuBtn->onMouseButton1ClickCb.connect(
        [this]() { s_showGrownMenu(m_transformMenu, m_transformMenuBtn, m_root); }));

    m_renderMenuBtn = m_viewportToolbar->add<IconLabelButton>(Icons::SVG_PERSPECTIVE, VIEWPORT_LIGHTING_MODE_LABELS[VLM_LIT],
                                                              buttonClasses);
    m_renderMenuBtn->setBaseProperties({.layoutOrder = 1});
    m_renderMenuBtn->track(
        m_renderMenuBtn->onMouseButton1ClickCb.connect([this]() { s_showGrownMenu(m_renderMenu, m_renderMenuBtn, m_root); }));

    fitViewportToolbar();
}

void ViewportPanel::fitViewportToolbar()
{
    const Amethyst::TextProcessor &textProcessor = m_context.editor->amethyst->getTextProcessor();
    const float buttonHeight = VIEWPORT_TOOLBAR_HEIGHT - 2.0f * VIEWPORT_TOOLBAR_PADDING;

    const float transformWidth = m_transformMenuBtn->measureFittedWidth(textProcessor);
    const float renderWidth = m_renderMenuBtn->measureFittedWidth(textProcessor);

    m_transformMenuBtn->setBaseProperties({.size = Amethyst::UDim2::fromOffset(transformWidth, buttonHeight)});
    m_renderMenuBtn->setBaseProperties({.size = Amethyst::UDim2::fromOffset(renderWidth, buttonHeight)});

    const float toolbarWidth = transformWidth + renderWidth + VIEWPORT_TOOLBAR_GAP + 2.0f * VIEWPORT_TOOLBAR_PADDING;
    m_viewportToolbar->setBaseProperties({.size = Amethyst::UDim2::fromOffset(toolbarWidth, VIEWPORT_TOOLBAR_HEIGHT)});
}

void ViewportPanel::buildTransformMenu()
{
    m_transformMenu = m_root->add<Amethyst::ContextMenu>();
    m_transformMenu->addClass("viewport-context-menu");
    m_transformMenu->popupWidth *= 1.3f;
    m_transformMenu->maxVisibleItems = INT_MAX;
    m_transformMenu->setRowFactories({
        .separator = [textProcessor = &m_context.editor->amethyst->getTextProcessor()] { return std::make_unique<SeparatorMenuItemView>(*textProcessor); },
        .radio = [] { return std::make_unique<RadioMenuItemView>(); },
    });

    m_gizmoOpGroupConn = m_gizmoOpGroup.onChanged.connect(
        [this]() { m_gizmoOperation = static_cast<gizmo::TransformGizmo::Operation>(m_gizmoOpGroup.value); });
    m_gizmoSpaceGroupConn = m_gizmoSpaceGroup.onChanged.connect(
        [this]() { m_gizmoSpace = static_cast<gizmo::TransformGizmo::Space>(m_gizmoSpaceGroup.value); });
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
        RadioMenuItem::create("Translate", &m_gizmoOpGroup, static_cast<int32_t>(gizmo::TransformGizmo::OPERATION_TRANSLATE)));
    items.push_back(
        RadioMenuItem::create("Rotate", &m_gizmoOpGroup, static_cast<int32_t>(gizmo::TransformGizmo::OPERATION_ROTATE)));
    items.push_back(RadioMenuItem::create("Scale", &m_gizmoOpGroup, static_cast<int32_t>(gizmo::TransformGizmo::OPERATION_SCALE)));
    items.push_back(SeparatorMenuItem::create("Space"));
    items.push_back(RadioMenuItem::create("World", &m_gizmoSpaceGroup, static_cast<int32_t>(gizmo::TransformGizmo::SPACE_WORLD)));
    items.push_back(RadioMenuItem::create("Local", &m_gizmoSpaceGroup, static_cast<int32_t>(gizmo::TransformGizmo::SPACE_LOCAL)));
    items.push_back(SeparatorMenuItem::create("Camera"));
    items.push_back(RadioMenuItem::create("Orbit", &m_cameraModeGroup, static_cast<int32_t>(Rapture::CameraControlMode::ORBIT)));
    items.push_back(RadioMenuItem::create("Fly", &m_cameraModeGroup, static_cast<int32_t>(Rapture::CameraControlMode::FLY)));
    m_transformMenu->setItems(std::move(items));
}

void ViewportPanel::buildRenderMenu()
{
    m_renderMenu = m_root->add<Amethyst::ContextMenu>();
    m_renderMenu->addClass("viewport-context-menu");
    m_renderMenu->popupWidth *= 1.3f;
    m_renderMenu->maxVisibleItems = INT_MAX;
    m_renderMenu->setRowFactories({.toggle = [] { return std::make_unique<ToggleMenuItemView>(); },
                                   .separator = [textProcessor = &m_context.editor->amethyst->getTextProcessor()] { return std::make_unique<SeparatorMenuItemView>(*textProcessor); },
                                   .radio = [] { return std::make_unique<RadioMenuItemView>(); }});

    m_lightingModeGroupConn = m_lightingModeGroup.onChanged.connect([this]() {
        const auto mode = static_cast<ViewportLightingMode>(m_lightingModeGroup.value);
        applyLightingMode(mode);
        m_renderMenuBtn->getLabel().setText(std::string(VIEWPORT_LIGHTING_MODE_LABELS[mode]));
        fitViewportToolbar();
    });

    m_probeDebugModeGroupConn = m_probeDebugModeGroup.onChanged.connect([this]() {
        if (m_viewport != nullptr) {
            m_viewport->renderSettings().probeDebugMode = static_cast<Rapture::ProbeDebugMode>(m_probeDebugModeGroup.value);
        }
    });

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
    items.push_back(SeparatorMenuItem::create("View mode"));
    for (ViewportLightingMode mode : {VLM_LIT, VLM_DIRECT_LIGHTING, VLM_INDIRECT_LIGHTING, VLM_RAW_IRRADIANCE, VLM_NORMALS, VLM_MOTION,
                                      VLM_AMBIENT_OCCLUSION}) {
        items.push_back(RadioMenuItem::create(std::string(VIEWPORT_LIGHTING_MODE_LABELS[mode]), &m_lightingModeGroup, mode));
    }

    items.push_back(SeparatorMenuItem::create("Features"));

    auto occlusionToggle = ToggleMenuItem::create("Ambient Occlusion", [this](bool on) {
        if (m_viewport != nullptr) {
            m_viewport->renderSettings().setFlag(Rapture::RENDER_USE_AMBIENT_OCCLUSION, on);
        }
    });
    occlusionToggle->as<ToggleMenuItem>().value = true;
    items.push_back(std::move(occlusionToggle));

    items.push_back(ToggleMenuItem::create("Show Probes", [this](bool on) {
        if (m_viewport != nullptr) {
            m_viewport->renderSettings().setFlag(Rapture::RENDER_SHOW_DDGI_PROBES, on);
        }
    }));

    items.push_back(SeparatorMenuItem::create("Probe debug"));
    items.push_back(RadioMenuItem::create("Classification", &m_probeDebugModeGroup, Rapture::PDM_CLASSIFICATION));
    items.push_back(RadioMenuItem::create("Irradiance", &m_probeDebugModeGroup, Rapture::PDM_IRRADIANCE));
    items.push_back(RadioMenuItem::create("Distance", &m_probeDebugModeGroup, Rapture::PDM_DISTANCE));
    items.push_back(RadioMenuItem::create("Relocation", &m_probeDebugModeGroup, Rapture::PDM_RELOCATION));

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
    settings.setFlag(Rapture::RENDER_SHOW_AMBIENT_OCCLUSION, false);
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
    case VLM_AMBIENT_OCCLUSION:
        settings.setFlag(Rapture::RENDER_SHOW_AMBIENT_OCCLUSION, true);
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
    if (m_context.editorCamera == nullptr) {
        return nullptr;
    }
    return &m_context.editorCamera->controller();
}

void ViewportPanel::updateEditorCamera(float dt)
{
    Rapture::CameraController *controller = cameraController();
    EditorCameraInput *input = m_context.editor->cameraInput;
    if (controller == nullptr || input == nullptr) {
        return;
    }

    // a camera holding the cursor keeps flying once the cursor leaves the image
    bool driven = m_viewportHovered || controller->capturesCursor();
    controller->setIntent(driven ? input->intent : Rapture::ControlInput{});
    controller->onUpdate(dt);

    if (controller->capturesCursor()) {
        input->cursorCaptured = true;
    }
}

void ViewportPanel::setViewportImage(Amethyst::AmTextureId imageId)
{
    if (m_viewportImage != nullptr) {
        m_viewportImage->setImage(imageId);
    }
}

void ViewportPanel::setPlayingOutlineVisible(bool visible)
{
    if (m_viewportImage == nullptr) {
        return;
    }

    if (visible) {
        m_viewportImage->addClass(VIEWPORT_IMAGE_PLAYING_CLASS);
    } else {
        m_viewportImage->removeClass(VIEWPORT_IMAGE_PLAYING_CLASS);
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
        UiTextures *textures = m_context.editor->textures;
        if (cached.id.isValid()) {
            textures->unregisterTexture(cached.id);
        }
        cached.id = textures->registerTexture(texture.get());
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

    updateEditorCamera(dt);

    auto *controller = cameraController();
    if (controller != nullptr) {
        m_cameraModeGroup.value = static_cast<int32_t>(controller->getMode());
    }

    updateViewportImage();

    auto *viewport = m_viewport;
    Amethyst::vec2 size = m_viewportImage->absoluteContentSize;
    if (auto *window = m_viewportImage->getWindow()) {
        size = Amethyst::vec2(std::round(size.x * window->getLogicalToPhysicalScale()),
                              std::round(size.y * window->getLogicalToPhysicalScale()));
    }
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
                        // the images this holds belong to the targets the resize destroys, and a new
                        // target can land on the address the old one had
                        releaseSlotImages();
                        viewport->resize(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));
                    }
                }
            }
        }
    }
}

bool ViewportPanel::toViewportPixel(const Amethyst::vec2 &windowPosition, glm::vec2 &pixel) const
{
    if (m_viewportImage == nullptr || m_viewport == nullptr) {
        return false;
    }

    const Amethyst::vec2 origin = m_viewportImage->absoluteContentPosition;
    const Amethyst::vec2 size = m_viewportImage->absoluteContentSize;
    if (size.x < 1.0f || size.y < 1.0f) {
        return false;
    }

    const float u = (windowPosition.x - origin.x) / size.x;
    const float v = (windowPosition.y - origin.y) / size.y;
    if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) {
        return false;
    }

    pixel = glm::vec2(u * static_cast<float>(m_viewport->getWidth()), v * static_cast<float>(m_viewport->getHeight()));
    return true;
}

void ViewportPanel::onViewportCursorMoved(const Amethyst::InputObject &input)
{
    toViewportPixel(input.position, m_cursorInViewport);
}

void ViewportPanel::onViewportMouseReleased(const Amethyst::InputObject &input)
{
    if (input.type != Amethyst::InputType::MOUSE_BUTTON_1) {
        return;
    }

    toViewportPixel(input.position, m_cursorInViewport);

    if (m_gizmoCapturing) {
        m_gizmoReleased = true;
        m_gizmoCapturing = false;
        if (auto *window = m_viewportImage->getWindow()) {
            window->releaseMouse(m_viewportImage);
        }
    }
}

void ViewportPanel::onViewportPressed(const Amethyst::InputObject &input)
{
    if (input.type != Amethyst::InputType::MOUSE_BUTTON_1) {
        return;
    }

    glm::vec2 pixel;
    if (!toViewportPixel(input.position, pixel)) {
        return;
    }

    m_cursorInViewport = pixel;

    // The gizmo answers from the handles it placed last frame, so a press on one is known before any
    // pick query runs
    if (m_transformGizmo->isHovered()) {
        m_gizmoPressed = true;
        m_gizmoCapturing = true;
        if (auto *window = m_viewportImage->getWindow()) {
            window->captureMouse(m_viewportImage);
        }
        return;
    }

    onImageClicked.fire();

    uint32_t px = static_cast<uint32_t>(pixel.x);
    uint32_t py = static_cast<uint32_t>(pixel.y);

    // Clamped rather than centred, so an aperture against a viewport edge keeps its full width and
    // the cursor simply sits off centre within it
    const uint32_t half = PICK_APERTURE / 2;
    Rapture::SceneQuery region;
    region.x = std::min(px > half ? px - half : 0u, m_viewport->getWidth() - 1);
    region.y = std::min(py > half ? py - half : 0u, m_viewport->getHeight() - 1);
    region.width = std::min(PICK_APERTURE, m_viewport->getWidth() - region.x);
    region.height = std::min(PICK_APERTURE, m_viewport->getHeight() - region.y);

    Rapture::SceneQueryResult result = m_viewport->queryRegion(region);
    std::optional<uint64_t> hit = s_nearestToCursor(result, px - region.x, py - region.y);

    Rapture::ecs::EntityAccessor picked = hit.has_value() ? Rapture::ecs::EntityAccessor(static_cast<Rapture::ecs::Entity>(*hit),
                                                                                         &m_viewport->getScene()->getRegistry())
                                                          : Rapture::ecs::EntityAccessor();
    if (m_selection != nullptr) {
        m_selection->select(picked);
    }
}

void ViewportPanel::updateGizmo()
{
    if (!m_selectedEntity.isValid()) {
        if (m_previousSelectedEntity.isValid()) {
            m_transformGizmo->reset();
            m_previousSelectedEntity = Rapture::ecs::EntityAccessor();
        }
        return;
    }

    if (m_selectedEntity != m_previousSelectedEntity) {
        m_transformGizmo->reset();
        m_previousSelectedEntity = m_selectedEntity;
    }

    const auto *meshComp = m_selectedEntity.tryRead<Rapture::StaticMeshComponent>();

    if (m_viewport == nullptr) {
        return;
    }
    auto *scene = m_viewport->getScene();
    if (scene == nullptr) {
        return;
    }

    Rapture::SceneObject *instance = scene->instanceFor(m_selectedEntity.getEntity());
    Rapture::Node3D *node = instance != nullptr ? instance->as<Rapture::Node3D>() : nullptr;
    if (node == nullptr) {
        return;
    }

    auto viewCamera = m_viewport->getCamera();
    if (!viewCamera.isValid()) {
        return;
    }

    const auto &camComp = viewCamera.read<Rapture::CameraComponent>();
    glm::mat4 viewMatrix = camComp.camera.getViewMatrix();
    glm::mat4 projectionMatrix = camComp.camera.getProjectionMatrix();
    glm::mat4 objectTransform = node->worldTransform();
    glm::vec3 pivot = glm::vec3(0.0f);
    if (meshComp != nullptr && meshComp->mesh) {
        const Rapture::Mesh &geometry = meshComp->mesh->geometry();
        pivot = Rapture::BoundingBox(geometry.getBoundsMin(), geometry.getBoundsMax()).getCenter();
    }

    gizmo::TransformGizmo::Params params;
    params.view = viewMatrix;
    params.projection = projectionMatrix;
    params.objectTransform = objectTransform;
    params.pivot = pivot;
    params.viewportSize = glm::vec2(m_viewport->getWidth(), m_viewport->getHeight());
    params.cursor = m_cursorInViewport;
    params.cursorInside = m_viewportHovered || m_gizmoCapturing;
    params.pressed = m_gizmoPressed;
    params.released = m_gizmoReleased;
    params.operation = m_gizmoOperation;
    params.space = m_gizmoSpace;

    m_gizmoPressed = false;
    m_gizmoReleased = false;

    gizmo::TransformGizmo::Result result = m_transformGizmo->update(params, m_viewport->getGizmoDrawList());

    if (!result.active) {
        return;
    }

    // A turn or a scale is about the handle's own pivot, which is not the object's origin whenever
    // the mesh sits off it, so the change is built around that point and applied on the outside
    const glm::mat4 toPivot = glm::translate(glm::mat4(1.0f), -result.pivot);
    const glm::mat4 fromPivot = glm::translate(glm::mat4(1.0f), result.pivot);
    const glm::mat4 basis = glm::mat4(result.basis);

    glm::mat4 delta(1.0f);

    switch (result.operation) {
    case gizmo::TransformGizmo::OPERATION_TRANSLATE:
        delta = glm::translate(glm::mat4(1.0f), result.deltaPosition);
        break;
    case gizmo::TransformGizmo::OPERATION_ROTATE: {
        const float angle = glm::length(result.deltaRotation);
        if (angle <= 0.0001f) {
            return;
        }
        delta = fromPivot * glm::rotate(glm::mat4(1.0f), angle, result.deltaRotation / angle) * toPivot;
        break;
    }
    case gizmo::TransformGizmo::OPERATION_SCALE:
        delta = fromPivot * basis * glm::scale(glm::mat4(1.0f), result.deltaScale) * glm::inverse(basis) * toPivot;
        break;
    case gizmo::TransformGizmo::OPERATION_COUNT:
        return;
    }

    node->setWorldTransform(delta * objectTransform);
}

#include "WorldEditorWorkspace.h"

#include "Icons.h"
#include "app/EditorConfig.h"
#include "layers/PlayLayer.h"
#include "panels/AddSceneObjectMenu.h"
#include "panels/ImagePreviewPanel.h"
#include "panels/OutlinerPanel.h"
#include "panels/PropertiesPanel.h"
#include "panels/ViewportPanel.h"
#include "panels/WorldSettingsPanel.h"
#include "ui/AssetVisuals.h"
#include "ui/menus/ContextMenuItems.h"

#include <components/extensions/ui_aspect_ratio_constraint.h>
#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>
#include <rapture/app/Application.h>
#include <rapture/app/Layer.h>
#include <rapture/core/utils/rp_assert.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Instance.h>

static constexpr float PLAY_CONTROL_SCALE = 0.7f;
static constexpr float PLAY_CONTROL_GAP = 6.0f;
static constexpr float OUTLINER_SPLIT = 0.25f;
static constexpr float VIEWPORT_SPLIT = 0.65f;
static constexpr uint32_t INITIAL_VIEWPORT_WIDTH = 1280;
static constexpr uint32_t INITIAL_VIEWPORT_HEIGHT = 720;
static constexpr std::string_view EDITOR_LAYER_NAME = "Editor Layer";

static void s_setButtonGuiState(Amethyst::ImageButton &button, uint16_t state, bool on)
{
    const uint16_t current = button.getGuiState();
    button.setGuiState(static_cast<uint16_t>(on ? (current | state) : (current & ~state)));
}

WorldEditorWorkspace::WorldEditorWorkspace(const EditorContext &context, Rapture::Ref<Rapture::AWorld> world)
    : AssetWorkspace(context, world.get()->handle()), m_world(std::move(world))
{
    RP_ASSERT(m_world, "a world editor has nothing to edit without a world");

    m_context.world = m_world.operator->();
    m_context.scene = m_world->getScene();
    setupViewport();
}

WorldEditorWorkspace::~WorldEditorWorkspace()
{
    destroyPanels();
    Rapture::Application::getInstance().getViewportManager().destroyViewport(m_viewport);
}

std::string_view WorldEditorWorkspace::tabIcon() const
{
    return Asset_iconForType(Rapture::ASSET_WORLD);
}

void WorldEditorWorkspace::setupViewport()
{
    auto &app = Rapture::Application::getInstance();

    m_viewport = app.getViewportManager().createViewport({
        .scene = m_context.scene,
        .camera = {},
        .name = m_world->getName(),
        .targetType = Rapture::SceneRenderTarget::TargetType::OFFSCREEN,
        .width = INITIAL_VIEWPORT_WIDTH,
        .height = INITIAL_VIEWPORT_HEIGHT,
        .framesInFlight = app.getFramesInFlight(),
    });
    m_viewport.viewport->createRenderer(Rapture::RendererType::DEFERRED);

    m_context.viewport = m_viewport.viewport;
}

void WorldEditorWorkspace::build()
{
    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::TabBar *outlinerTabBar = nullptr;
    Amethyst::TabBar *propertiesTabBar = nullptr;

    Amethyst::DockScope(*m_contentDock)
        .split(
            Amethyst::SplitAxis::VERTICAL, OUTLINER_SPLIT,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { outlinerTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) {
                r.split(
                    Amethyst::SplitAxis::HORIZONTAL, VIEWPORT_SPLIT,
                    [&](Amethyst::DockScope &t) { t.panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; }); },
                    [&](Amethyst::DockScope &b) { b.panel([&](Amethyst::TabBarScope &tb) { propertiesTabBar = &tb.component; }); });
            });

    auto *viewportPanel = addPanel(std::make_unique<ViewportPanel>(m_context), viewportTabBar);
    m_viewportClickedConn = viewportPanel->onImageClicked.connect([this]() {
        if (m_playLayer != nullptr && isPlaying()) {
            m_playLayer->takeControl();
        }
    });

    addPanel(std::make_unique<OutlinerPanel>(m_context), outlinerTabBar);
    addPanel(std::make_unique<PropertiesPanel>(m_context), propertiesTabBar);
    addPanel(std::make_unique<WorldSettingsPanel>(m_context), propertiesTabBar);
    addPanel(std::make_unique<ImagePreviewPanel>(m_context, "Texture Viewer", ImagePreviewMode::ASSET_PICKER), propertiesTabBar);

    m_addMenu = m_container->add<Amethyst::ContextMenu>();
    m_addMenu->setRowFactories({.separator = [] { return std::make_unique<SeparatorMenuItemView>(); }});

    setupShortcuts();
}

void WorldEditorWorkspace::buildHotbar()
{
    m_addButton = addHotbarButton("Add", [this]() { showAddMenu(*m_addButton); });

    addHotbarSeparator();
    addHotbarButton("Save World", [this]() { saveWorld(); });
    addHotbarSeparator();

    const float buttonSize = config().getUnscaledFloat(EDITOR_CONFIG_HOTBAR_HEIGHT) * PLAY_CONTROL_SCALE;
    const float groupWidth = 3.0f * buttonSize + 2.0f * PLAY_CONTROL_GAP;
    addHotbarItem(groupWidth, [this](Amethyst::UIScope &slot) { setupPlayControls(*slot.get().as<Amethyst::Frame>()); });
}

void WorldEditorWorkspace::setupPlayControls(Amethyst::Frame &group)
{
    Amethyst::UIScope controls(group);

    auto *layout = group.addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    layout->innerPadding = Amethyst::UDim::fromOffset(PLAY_CONTROL_GAP);

    controls.imageButton(
        {
            .classes = {"generic-text-button", "play-button"},
            .base = {.layoutOrder = 0, .size = Amethyst::UDim2(0.0f, 0.0f, PLAY_CONTROL_SCALE, 0.0f)},
            .svg = Icons::SVG_PLAY,
        },
        [this](Amethyst::ImageButtonScope &b) {
            m_playButton = &b.component;
            b.component.addExtension<Amethyst::UIAspectRatioConstraint>()->dominantAxis = Amethyst::DominantAxis::HEIGHT;
            b.component.onMouseButton1ClickCb = [this]() {
                if (isPlaying()) {
                    stopPlay();
                } else {
                    startPlay();
                }
                return Amethyst::EventResult::CONSUMED;
            };
        });

    controls.imageButton(
        {
            .classes = {"generic-text-button"},
            .base = {.layoutOrder = 1, .size = Amethyst::UDim2(0.0f, 0.0f, PLAY_CONTROL_SCALE, 0.0f)},
            .svg = Icons::SVG_PAUSE,
        },
        [this](Amethyst::ImageButtonScope &b) {
            m_pauseButton = &b.component;
            b.component.addExtension<Amethyst::UIAspectRatioConstraint>()->dominantAxis = Amethyst::DominantAxis::HEIGHT;
            b.component.onMouseButton1ClickCb = [this]() {
                togglePause();
                return Amethyst::EventResult::CONSUMED;
            };
        });

    controls.imageButton(
        {
            .classes = {"generic-text-button"},
            .base = {.layoutOrder = 2, .size = Amethyst::UDim2(0.0f, 0.0f, PLAY_CONTROL_SCALE, 0.0f)},
            .svg = Icons::SVG_FRAME_ADVANCE,
        },
        [this](Amethyst::ImageButtonScope &b) {
            m_stepButton = &b.component;
            b.component.addExtension<Amethyst::UIAspectRatioConstraint>()->dominantAxis = Amethyst::DominantAxis::HEIGHT;
        });
}

void WorldEditorWorkspace::setupShortcuts()
{
    ShortcutRegistry *shortcuts = m_editor.shortcuts;

    shortcuts->registerShortcut(EDITOR_COMMAND_PLAY_MODE_TOGGLE_CONTROL, {Rapture::KEY_F8}, m_container, [this]() {
        if (m_playLayer != nullptr && isPlaying()) {
            m_playLayer->toggleControl();
        }
    });

    shortcuts->registerShortcut(EDITOR_COMMAND_PLAY_MODE_STOP, {Rapture::KEY_ESCAPE}, m_container, [this]() { stopPlay(); });
}

void WorldEditorWorkspace::togglePause()
{
    if (!isPlaying()) {
        return;
    }

    if (m_world->playState() == Rapture::PlayState::PAUSED) {
        m_world->resume();
    } else {
        m_world->pause();
    }

    syncPlayButtons();
}

void WorldEditorWorkspace::syncPlayButtons()
{
    const bool playing = isPlaying();
    m_playButton->setSvg(playing ? Icons::SVG_STOP : Icons::SVG_PLAY);
    s_setButtonGuiState(*m_playButton, Amethyst::GUI_STATE_ACTIVE, playing);

    const bool paused = m_world->playState() == Rapture::PlayState::PAUSED;
    m_pauseButton->setSvg(paused ? Icons::SVG_PLAY : Icons::SVG_PAUSE);
    s_setButtonGuiState(*m_pauseButton, Amethyst::GUI_STATE_ACTIVE, paused);
}

void WorldEditorWorkspace::startPlay()
{
    if (m_context.viewport == nullptr || isPlaying()) {
        return;
    }

    auto &app = Rapture::Application::getInstance();
    if (Rapture::Layer *editor = app.getLayer(EDITOR_LAYER_NAME)) {
        editor->detach();
    }

    if (m_playLayer == nullptr) {
        m_playLayer =
            static_cast<PlayLayer *>(app.pushLayer(std::make_unique<PlayLayer>(m_world.get()->world(), *m_context.viewport)));
    } else {
        m_playLayer->attach();
    }

    syncPlayButtons();
}

void WorldEditorWorkspace::stopPlay()
{
    if (!isPlaying()) {
        return;
    }

    if (m_playLayer != nullptr) {
        m_playLayer->detach();
    }

    if (Rapture::Layer *editor = Rapture::Application::getInstance().getLayer(EDITOR_LAYER_NAME)) {
        editor->attach();
    }

    syncPlayButtons();
}

void WorldEditorWorkspace::showAddMenu(Amethyst::TextButton &button)
{
    if (m_addMenu == nullptr) {
        return;
    }

    m_addMenu->setItems(AddSceneObjectMenu_buildItems(m_world->getScene()->root(), m_selection, SCENE_OBJECT_SCOPE_LEVEL));
    m_addMenu->showAt({button.absolutePosition.x, button.absolutePosition.y + button.absoluteSize.y});
}

void WorldEditorWorkspace::saveWorld()
{
    auto &project = Rapture::Application::getInstance().getProject();
    if (!project.saveWorld(m_world.get()->handle())) {
        return;
    }

    project.saveProject(project.getProjectFilePath());
}

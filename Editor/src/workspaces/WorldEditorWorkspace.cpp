#include "WorldEditorWorkspace.h"

#include "Icons.h"
#include "layers/PlayLayer.h"
#include "panels/AddSceneObjectMenu.h"
#include "panels/ImagePreviewPanel.h"
#include "panels/OutlinerPanel.h"
#include "panels/PropertiesPanel.h"
#include "panels/ViewportPanel.h"
#include "panels/WorldSettingsPanel.h"
#include "scene/EditorCamera.h"
#include "ui/AssetVisuals.h"
#include "ui/ControlTooltip.h"
#include "ui/controls/IconButton.h"
#include "ui/controls/IconLabelButton.h"
#include "ui/menus/ContextMenuItems.h"

#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>
#include <rapture/app/Application.h>
#include <rapture/app/Layer.h>
#include <rapture/core/utils/rp_assert.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Camera3D.h>
#include <rapture/scene/instances/Instance.h>

#include <array>

static constexpr std::string_view ADD_BUTTON_CLASS = "hotbar-primary-button";

static constexpr float PLAY_BUTTON_WIDTH = 44.0f;
static constexpr float PLAY_BUTTON_HEIGHT = 28.0f;
static constexpr float PLAY_ICON_SIZE = 18.0f;
static constexpr float PLAY_CONTROL_GAP = 2.0f;
static constexpr float PLAY_GROUP_PADDING = 2.0f;
static constexpr float PLAY_GROUP_BORDER = 1.0f;

static constexpr float PLAYING_NOTE_WIDTH = 340.0f;
static constexpr float PLAYING_NOTE_DOT_SIZE = 8.0f;
static constexpr float PLAYING_NOTE_GAP = 8.0f;
static constexpr float OUTLINER_SPLIT = 0.25f;
static constexpr float VIEWPORT_SPLIT = 0.65f;
static constexpr uint32_t INITIAL_VIEWPORT_WIDTH = 1280;
static constexpr uint32_t INITIAL_VIEWPORT_HEIGHT = 720;
static constexpr std::string_view EDITOR_LAYER_NAME = "Editor Layer";

enum WorldEditorTooltip {
    WORLD_EDITOR_TOOLTIP_ADD,
    WORLD_EDITOR_TOOLTIP_SAVE_WORLD,
    WORLD_EDITOR_TOOLTIP_PLAY,
    WORLD_EDITOR_TOOLTIP_STOP,
    WORLD_EDITOR_TOOLTIP_PAUSE,
    WORLD_EDITOR_TOOLTIP_RESUME,
    WORLD_EDITOR_TOOLTIP_STEP,
    WORLD_EDITOR_TOOLTIP_COUNT
};

static constexpr std::array<ControlTooltip, WORLD_EDITOR_TOOLTIP_COUNT> WORLD_EDITOR_TOOLTIPS = {{
    {.name = "Add", .description = "Adds an object to the root of the world"},
    {.name = "Save World", .description = "Writes this world and its objects to its asset file"},
    {.name = "Play", .description = "Play the world"},
    {.name = "Stop", .description = "Stop playing", .command = EDITOR_COMMAND_PLAY_MODE_STOP},
    {.name = "Pause", .description = "Pause the world"},
    {.name = "Resume", .description = "Resume the world"},
    {.name = "Step", .description = "Advance one frame"},
}};

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
    m_editorCamera.reset();
}

std::string_view WorldEditorWorkspace::tabIcon() const
{
    return Asset_iconForType(Rapture::ASSET_WORLD);
}

void WorldEditorWorkspace::setupViewport()
{
    auto &app = Rapture::Application::getInstance();

    m_editorCamera = std::make_unique<EditorCamera>(*m_context.scene);

    m_viewport = app.getViewportManager().createViewport({
        .scene = m_context.scene,
        .camera = m_editorCamera->camera().accessor(),
        .name = m_world->getName(),
        .targetType = Rapture::SceneRenderTarget::TargetType::OFFSCREEN,
        .width = INITIAL_VIEWPORT_WIDTH,
        .height = INITIAL_VIEWPORT_HEIGHT,
        .framesInFlight = app.getFramesInFlight(),
    });
    m_viewport.viewport->createRenderer(Rapture::RendererType::DEFERRED);

    m_context.viewport = m_viewport.viewport;
    m_context.editorCamera = m_editorCamera.get();
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

    auto *viewportPanel = addPanel(std::make_unique<ViewportPanel>(m_context, ViewportOverlays{.toolbar = true, .orientationGizmo = true}), viewportTabBar);
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
    m_addMenu->setRowFactories({.separator = [textProcessor = &m_editor.amethyst->getTextProcessor()] { return std::make_unique<SeparatorMenuItemView>(*textProcessor); }});

    setupShortcuts();
}

void WorldEditorWorkspace::buildHotbar()
{
    m_addButton = addHotbarButton("Add", Icons::SVG_PLUS, ADD_BUTTON_CLASS, [this]() { showAddMenu(*m_addButton); });
    ControlTooltip_attach(*m_addButton, WORLD_EDITOR_TOOLTIPS[WORLD_EDITOR_TOOLTIP_ADD], m_editor);
    addHotbarSeparator();
    IconLabelButton *saveButton = addHotbarButton("Save World", Icons::SVG_SAVE, [this]() { saveWorld(); });
    ControlTooltip_attach(*saveButton, WORLD_EDITOR_TOOLTIPS[WORLD_EDITOR_TOOLTIP_SAVE_WORLD], m_editor);
    addHotbarSeparator();

    const float groupInset = PLAY_GROUP_BORDER + PLAY_GROUP_PADDING;
    const Amethyst::vec2 groupSize(3.0f * PLAY_BUTTON_WIDTH + 2.0f * PLAY_CONTROL_GAP + 2.0f * groupInset,
                                   PLAY_BUTTON_HEIGHT + 2.0f * groupInset);
    addHotbarItem(groupSize, [this](Amethyst::UIScope &slot) { setupPlayControls(*slot.get().as<Amethyst::Frame>()); });
    addHotbarSeparator();
    addPlayingNote();
}

void WorldEditorWorkspace::addPlayingNote()
{
    m_playingNote = addHotbarItem(PLAYING_NOTE_WIDTH, [](Amethyst::UIScope &slot) {
        slot.frame({
            .classes = {"playing-note-dot"},
            .base =
                {
                    .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                    .interactable = false,
                    .position = Amethyst::UDim2(0.0f, 0.0f, 0.5f, 0.0f),
                    .size = Amethyst::UDim2::fromOffset(PLAYING_NOTE_DOT_SIZE, PLAYING_NOTE_DOT_SIZE),
                },
        });
        slot.textLabel({
            .classes = {"playing-note"},
            .base =
                {
                    .interactable = false,
                    .position = Amethyst::UDim2(0.0f, PLAYING_NOTE_DOT_SIZE + PLAYING_NOTE_GAP, 0.0f, 0.0f),
                    .size = Amethyst::UDim2(1.0f, -(PLAYING_NOTE_DOT_SIZE + PLAYING_NOTE_GAP), 1.0f, 0.0f),
                },
            .label = "Playing, changes made now are undone when you stop",
        });
    });
    m_playingNote->setBaseProperties({.visible = false});
}

void WorldEditorWorkspace::syncPlayingIndicators()
{
    const bool playing = isPlaying();
    m_playingNote->setBaseProperties({.visible = playing});

    for (const auto &panel : getPanels()) {
        if (auto *viewportPanel = dynamic_cast<ViewportPanel *>(panel.get())) {
            viewportPanel->setPlayingOutlineVisible(playing);
        }
    }
}

void WorldEditorWorkspace::setupPlayControls(Amethyst::Frame &group)
{
    group.setClasses({"hotbar-play-group"});
    group.setBaseProperties({.padding = Amethyst::UDim4::fromOffset(PLAY_GROUP_PADDING)});

    auto *layout = group.addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    layout->innerPadding = Amethyst::UDim::fromOffset(PLAY_CONTROL_GAP);

    const Amethyst::UDim2 buttonSize = Amethyst::UDim2::fromOffset(PLAY_BUTTON_WIDTH, PLAY_BUTTON_HEIGHT);
    const std::vector<std::string> buttonClasses = {"hotbar-play-button"};

    const std::vector<std::string> playButtonClasses = {"hotbar-play-button", "play-button"};
    m_playButton = group.add<IconButton>(Icons::SVG_PLAY, PLAY_ICON_SIZE, playButtonClasses);
    m_playButton->setBaseProperties({.layoutOrder = 0, .size = buttonSize});
    m_playButton->track(m_playButton->onMouseButton1ClickCb.connect([this]() {
        if (isPlaying()) {
            stopPlay();
        } else {
            startPlay();
        }
    }));

    m_pauseButton = group.add<IconButton>(Icons::SVG_PAUSE, PLAY_ICON_SIZE, buttonClasses);
    m_pauseButton->setBaseProperties({.layoutOrder = 1, .size = buttonSize});
    m_pauseButton->track(m_pauseButton->onMouseButton1ClickCb.connect([this]() { togglePause(); }));

    m_stepButton = group.add<IconButton>(Icons::SVG_FRAME_ADVANCE, PLAY_ICON_SIZE, buttonClasses);
    m_stepButton->setBaseProperties({.layoutOrder = 2, .size = buttonSize});

    ControlTooltip_attach(*m_playButton, WORLD_EDITOR_TOOLTIPS[WORLD_EDITOR_TOOLTIP_PLAY], m_editor);
    ControlTooltip_attach(*m_pauseButton, WORLD_EDITOR_TOOLTIPS[WORLD_EDITOR_TOOLTIP_PAUSE], m_editor);
    ControlTooltip_attach(*m_stepButton, WORLD_EDITOR_TOOLTIPS[WORLD_EDITOR_TOOLTIP_STEP], m_editor);
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
    m_playButton->setIcon(playing ? Icons::SVG_STOP : Icons::SVG_PLAY);
    m_playButton->setGuiStateFlag(Amethyst::GUI_STATE_ACTIVE, playing);
    ControlTooltip_attach(*m_playButton, WORLD_EDITOR_TOOLTIPS[playing ? WORLD_EDITOR_TOOLTIP_STOP : WORLD_EDITOR_TOOLTIP_PLAY],
                          m_editor);

    const bool paused = m_world->playState() == Rapture::PlayState::PAUSED;
    m_pauseButton->setIcon(paused ? Icons::SVG_PLAY : Icons::SVG_PAUSE);
    m_pauseButton->setGuiStateFlag(Amethyst::GUI_STATE_ACTIVE, paused);
    ControlTooltip_attach(*m_pauseButton, WORLD_EDITOR_TOOLTIPS[paused ? WORLD_EDITOR_TOOLTIP_RESUME : WORLD_EDITOR_TOOLTIP_PAUSE],
                          m_editor);
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
    syncPlayingIndicators();
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
    syncPlayingIndicators();
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

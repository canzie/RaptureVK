#include "LevelEditorWorkspace.h"

#include "Icons.h"
#include "layers/PlayLayer.h"
#include "layers/panels/AddSceneObjectMenu.h"
#include "layers/panels/ImagePreviewPanel.h"
#include "layers/panels/OutlinerPanel.h"
#include "layers/panels/PropertiesPanel.h"
#include "layers/panels/ViewportPanel.h"
#include "layers/panels/WorldSettingsPanel.h"
#include "layers/panels/components/context_menus.h"

#include <components/extensions/ui_aspect_ratio_constraint.h>
#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>
#include <app/Layer.h>
#include <scene/Scene.h>
#include <scene/instances/Instance.h>
#include <core/utils/rp_assert.h>
#include <app/Application.h>

static constexpr float HOTBAR_BUTTON_WIDTH = 90.0f;
static constexpr uint32_t INITIAL_VIEWPORT_WIDTH = 1280;
static constexpr uint32_t INITIAL_VIEWPORT_HEIGHT = 720;
static constexpr std::string_view EDITOR_LAYER_NAME = "Editor Layer";

static void s_setButtonGuiState(Amethyst::ImageButton &button, uint16_t state, bool on)
{
    const uint16_t current = button.getGuiState();
    button.setGuiState(static_cast<uint16_t>(on ? (current | state) : (current & ~state)));
}

LevelEditorWorkspace::LevelEditorWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services,
                                           Rapture::AssetPtr<Rapture::World> world)
    : Workspace(staticKind()), m_world(std::move(world))
{
    RP_ASSERT(m_world, "a level editor has nothing to edit without a world");

    m_context.services = services;
    m_context.world = m_world.get();
    m_context.scene = m_world->getScene();
    setupViewport();
    setupBase(tabBar, "Level Editor", {});

    m_dockingLayer->name = "Editor Dock";
    m_dockingLayer->tabBarClasses = {"panel", "panel-tab"};

    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::TabBar *outlinerTabBar = nullptr;
    Amethyst::TabBar *propertiesTabBar = nullptr;

    Amethyst::DockScope(*m_dockingLayer)
        .split(
            Amethyst::SplitAxis::VERTICAL, 0.25f,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { outlinerTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) {
                r.split(
                    Amethyst::SplitAxis::HORIZONTAL, 0.65f,
                    [&](Amethyst::DockScope &t) { t.panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; }); },
                    [&](Amethyst::DockScope &b) { b.panel([&](Amethyst::TabBarScope &tb) { propertiesTabBar = &tb.component; }); });
            });

    if (viewportTabBar != nullptr) {
        auto viewportPanel = std::make_unique<ViewportPanel>(viewportTabBar, m_context);
        m_viewportClickedConn = viewportPanel->onImageClicked.connect([this]() {
            if (m_playLayer != nullptr && isPlaying()) {
                m_playLayer->takeControl();
            }
        });
        m_panels.push_back(std::move(viewportPanel));
    }
    if (outlinerTabBar != nullptr) {
        m_panels.push_back(std::make_unique<OutlinerPanel>(outlinerTabBar, m_context));
    }
    if (propertiesTabBar != nullptr) {
        m_panels.push_back(std::make_unique<PropertiesPanel>(propertiesTabBar, m_context));
        m_panels.push_back(std::make_unique<WorldSettingsPanel>(propertiesTabBar, m_context));
        m_panels.push_back(
            std::make_unique<ImagePreviewPanel>(propertiesTabBar, m_context, "Texture Viewer", ImagePreviewMode::ASSET_PICKER));
    }

    setupHotbar();
    setupShortcuts();

    if (Amethyst::LayoutConfig::instance().loadFromFile("layout.conf")) {
        if (auto *entry = Amethyst::LayoutConfig::instance().get("Editor Dock")) {
            if (entry->type == Amethyst::ConfigType::DOCK_LAYOUT) {
                m_dockingLayer->applyConfig(entry->dockLayout);
            }
        }
    }
}

LevelEditorWorkspace::~LevelEditorWorkspace()
{
    m_panels.clear();
    Rapture::Application::getInstance().getViewportManager().destroyViewport(m_viewport);
}

void LevelEditorWorkspace::setupViewport()
{
    auto &app = Rapture::Application::getInstance();

    m_viewport = app.getViewportManager().createViewport({
        .name = m_world->getName(),
        .targetType = Rapture::SceneRenderTarget::TargetType::OFFSCREEN,
        .width = INITIAL_VIEWPORT_WIDTH,
        .height = INITIAL_VIEWPORT_HEIGHT,
        .framesInFlight = app.getFramesInFlight(),
    });
    m_viewport.viewport->createRenderer(Rapture::RendererType::DEFERRED);
    m_viewport.viewport->setScene(m_context.scene);

    m_context.viewport = m_viewport.viewport;
}

// TODO: Add the separators, 1px full height bg-window color, around 8px space to the left and right of it, so 19px between each
// element
void LevelEditorWorkspace::setupHotbar()
{
    if (m_hotbar == nullptr) {
        return;
    }

    auto *layout = m_hotbar->addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    layout->innerPadding = Amethyst::UDim::fromOffset(18.0f);

    m_addMenu = m_container->add<Amethyst::ContextMenu>();
    m_addMenu->setRowFactories({.separator = [] { return std::make_unique<ViewportContextMenuSIV>(); }});

    Amethyst::UIScope(*m_hotbar).textButton(
        {.classes = {"generic-text-button"},
         .base = {.layoutOrder = 0, .size = Amethyst::UDim2(0.0f, HOTBAR_BUTTON_WIDTH, 0.6f, 0.0f)},
         .text = {.fontSize = 14.0f},
         .label = "Add"},
        [this](Amethyst::TextButtonScope &b) {
            b.component.onMouseButton1ClickCb = [this, button = &b.component]() {
                showAddMenu(*button);
                return Amethyst::EventResult::CONSUMED;
            };
        });

    Amethyst::UIScope(*m_hotbar).textButton(
        {.classes = {"generic-text-button"},
         .base = {.layoutOrder = 1, .size = Amethyst::UDim2(0.0f, HOTBAR_BUTTON_WIDTH, 0.6f, 0.0f)},
         .text = {.fontSize = 14.0f},
         .label = "Save World"},
        [this](Amethyst::TextButtonScope &b) {
            b.component.onMouseButton1ClickCb = [this]() {
                saveWorld();
                return Amethyst::EventResult::CONSUMED;
            };
        });

    // TODO: put these 3 into 1 container frame, since they belong with eachother
    Amethyst::UIScope(*m_hotbar).imageButton(
        {
            .classes = {"generic-text-button", "play-button"},
            .base = {.layoutOrder = 2, .size = Amethyst::UDim2(0.0f, 0.0f, 0.7f, 0.0f)},
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

    Amethyst::UIScope(*m_hotbar).imageButton(
        {
            .classes = {"generic-text-button"},
            .base = {.layoutOrder = 3, .size = Amethyst::UDim2(0.0f, 0.0f, 0.7f, 0.0f)},
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

    Amethyst::UIScope(*m_hotbar).imageButton(
        {
            .classes = {"generic-text-button"},
            .base = {.layoutOrder = 4, .size = Amethyst::UDim2(0.0f, 0.0f, 0.7f, 0.0f)},
            .svg = Icons::SVG_FRAME_ADVANCE,
        },
        [this](Amethyst::ImageButtonScope &b) {
            m_stepButton = &b.component;
            b.component.addExtension<Amethyst::UIAspectRatioConstraint>()->dominantAxis = Amethyst::DominantAxis::HEIGHT;
        });
}

void LevelEditorWorkspace::setupShortcuts()
{
    const PanelServices &services = m_context.services;

    services.registerShortcut(EDITOR_COMMAND_PLAY_MODE_TOGGLE_CONTROL, {Rapture::KEY_F8}, m_container, [this]() {
        if (m_playLayer != nullptr && isPlaying()) {
            m_playLayer->toggleControl();
        }
    });

    services.registerShortcut(EDITOR_COMMAND_PLAY_MODE_STOP, {Rapture::KEY_ESCAPE}, m_container, [this]() { stopPlay(); });
}

void LevelEditorWorkspace::togglePause()
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

void LevelEditorWorkspace::syncPlayButtons()
{
    const bool playing = isPlaying();
    m_playButton->setSvg(playing ? Icons::SVG_STOP : Icons::SVG_PLAY);
    s_setButtonGuiState(*m_playButton, Amethyst::GUI_STATE_ACTIVE, playing);

    const bool paused = m_world->playState() == Rapture::PlayState::PAUSED;
    m_pauseButton->setSvg(paused ? Icons::SVG_PLAY : Icons::SVG_PAUSE);
    s_setButtonGuiState(*m_pauseButton, Amethyst::GUI_STATE_ACTIVE, paused);
}

void LevelEditorWorkspace::startPlay()
{
    if (m_context.viewport == nullptr || isPlaying()) {
        return;
    }

    auto &app = Rapture::Application::getInstance();
    if (Rapture::Layer *editor = app.getLayer(EDITOR_LAYER_NAME)) {
        editor->detach();
    }

    if (m_playLayer == nullptr) {
        m_playLayer = static_cast<PlayLayer *>(app.pushLayer(std::make_unique<PlayLayer>(*m_world, *m_context.viewport)));
    } else {
        m_playLayer->attach();
    }

    syncPlayButtons();
}

void LevelEditorWorkspace::stopPlay()
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

void LevelEditorWorkspace::showAddMenu(Amethyst::TextButton &button)
{
    if (m_addMenu == nullptr) {
        return;
    }

    m_addMenu->setItems(AddSceneObjectMenu_buildItems(m_world->getScene()->root(), m_selection, SCENE_OBJECT_SCOPE_LEVEL));
    m_addMenu->showAt({button.absolutePosition.x, button.absolutePosition.y + button.absoluteSize.y});
}

void LevelEditorWorkspace::saveWorld()
{
    auto &project = Rapture::Application::getInstance().getProject();
    if (!project.saveWorld(m_world.ref().get()->getHandle())) {
        return;
    }

    project.saveProject(project.getProjectFilePath());
}

void LevelEditorWorkspace::saveLayout()
{
    Amethyst::LayoutConfig::instance().set(m_dockingLayer->name, Amethyst::ConfigEntry(m_dockingLayer->saveConfig()));
}

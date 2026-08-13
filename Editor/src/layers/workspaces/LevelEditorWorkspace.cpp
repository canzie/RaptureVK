#include "LevelEditorWorkspace.h"

#include "layers/PlayLayer.h"
#include "layers/panels/AddSceneObjectMenu.h"
#include "layers/panels/components/context_menus.h"
#include "layers/panels/ImagePreviewPanel.h"
#include "layers/panels/OutlinerPanel.h"
#include "layers/panels/PropertiesPanel.h"
#include "layers/panels/ViewportPanel.h"
#include "layers/panels/WorldSettingsPanel.h"

#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>
#include <layers/Layer.h>
#include <scenes/Scene.h>
#include <scenes/instances/Instance.h>
#include <utils/rp_assert.h>
#include <window_context/Application.h>

static constexpr float HOTBAR_BUTTON_WIDTH = 90.0f;
static constexpr uint32_t INITIAL_VIEWPORT_WIDTH = 1280;
static constexpr uint32_t INITIAL_VIEWPORT_HEIGHT = 720;
static constexpr std::string_view EDITOR_LAYER_NAME = "Editor Layer";

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
        m_panels.push_back(std::make_unique<ViewportPanel>(viewportTabBar, m_context));
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
    });
    m_viewport.viewport->createRenderer(Rapture::RendererType::DEFERRED);
    m_viewport.viewport->setScene(m_context.scene);

    m_context.viewport = m_viewport.viewport;
}

void LevelEditorWorkspace::setupHotbar()
{
    if (m_hotbar == nullptr) {
        return;
    }

    auto *layout = m_hotbar->addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    layout->innerPadding = Amethyst::UDim::fromOffset(6.0f);

    m_addMenu = m_container->add<Amethyst::ContextMenu>();
    m_addMenu->setRowFactories({.separator = [] { return std::make_unique<ViewportContextMenuSIV>(); }});

    Amethyst::UIScope(*m_hotbar).textButton(
        {.classes = {"generic-text-button"},
         .base = {.layoutOrder = 0, .size = Amethyst::UDim2(0.0f, HOTBAR_BUTTON_WIDTH, 1.0f, 0.0f)},
         .label = "Add"},
        [this](Amethyst::TextButtonScope &b) {
            b.component.onMouseButton1ClickCb = [this, button = &b.component]() {
                showAddMenu(*button);
                return Amethyst::EventResult::CONSUMED;
            };
        });

    Amethyst::UIScope(*m_hotbar).textButton(
        {.classes = {"generic-text-button"},
         .base = {.layoutOrder = 1, .size = Amethyst::UDim2(0.0f, HOTBAR_BUTTON_WIDTH, 1.0f, 0.0f)},
         .label = "Save World"},
        [this](Amethyst::TextButtonScope &b) {
            b.component.onMouseButton1ClickCb = [this]() {
                saveWorld();
                return Amethyst::EventResult::CONSUMED;
            };
        });

    Amethyst::UIScope(*m_hotbar).textButton(
        {.classes = {"generic-text-button"},
         .base = {.layoutOrder = 2, .size = Amethyst::UDim2(0.0f, HOTBAR_BUTTON_WIDTH, 1.0f, 0.0f)},
         .label = "Play"},
        [this](Amethyst::TextButtonScope &b) {
            m_playButton = &b.component;
            b.component.onMouseButton1ClickCb = [this]() {
                if (isPlaying()) {
                    stopPlay();
                } else {
                    startPlay();
                }
                return Amethyst::EventResult::CONSUMED;
            };
        });
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
        m_playLayer = app.pushLayer(std::make_unique<PlayLayer>(*m_world, *m_context.viewport));
    } else {
        m_playLayer->attach();
    }

    m_playButton->setText("Stop");
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

    m_playButton->setText("Play");
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

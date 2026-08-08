#include "LevelEditorWorkspace.h"

#include "layers/PlayLayer.h"
#include "layers/panels/AddSceneObjectMenu.h"
#include "layers/panels/ImagePreviewPanel.h"
#include "layers/panels/OutlinerPanel.h"
#include "layers/panels/PropertiesPanel.h"
#include "layers/panels/ViewportPanel.h"

#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>
#include <layers/Layer.h>
#include <scenes/Scene.h>
#include <serialization/SerialDocument.h>
#include <scenes/instances/Instance.h>
#include <window_context/Application.h>

static constexpr float HOTBAR_BUTTON_WIDTH = 90.0f;
static constexpr std::string_view EDITOR_LAYER_NAME = "Editor Layer";

LevelEditorWorkspace::LevelEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services,
                                           Rapture::AssetPtr<Rapture::World> world, Rapture::Viewport *viewport)
    : m_world(std::move(world))
{
    m_context.services = services;
    m_context.world = m_world.get();
    m_context.viewport = viewport;
    setupBase(tabs, "Level Editor");

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
         .label = "Save Scene"},
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
    Rapture::Scene *scene = m_world ? m_world->getScene() : nullptr;
    if (scene == nullptr || m_context.viewport == nullptr || isPlaying()) {
        return;
    }

    m_snapshot = scene->snapshot();

    auto &app = Rapture::Application::getInstance();
    if (Rapture::Layer *editor = app.getLayer(EDITOR_LAYER_NAME)) {
        editor->detach();
    }

    if (m_playLayer == nullptr) {
        m_playLayer = app.pushLayer(std::make_unique<PlayLayer>(*scene, *m_context.viewport));
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

    // the snapshot outlives the rewind, its document is what the scene is read back out of
    m_world->getScene()->restoreFrom(m_snapshot.rootView());
    m_snapshot = Rapture::SerialDocument{};

    if (Rapture::Layer *editor = Rapture::Application::getInstance().getLayer(EDITOR_LAYER_NAME)) {
        editor->attach();
    }

    m_playButton->setText("Play");
}

void LevelEditorWorkspace::showAddMenu(Amethyst::TextButton &button)
{
    if (m_addMenu == nullptr || !m_world) {
        return;
    }

    m_addMenu->setItems(AddSceneObjectMenu::buildItems(m_world->getScene()->root()));
    m_addMenu->showAt({button.absolutePosition.x, button.absolutePosition.y + button.absoluteSize.y});
}

void LevelEditorWorkspace::saveWorld()
{
    if (!m_world) {
        return;
    }

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

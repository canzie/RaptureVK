#include "ModuleEditorWorkspace.h"
#include <assets/modules/AModule.h>

#include "Icons.h"
#include "layers/panels/PropertiesPanel.h"
#include "layers/panels/SceneObjectTreePanel.h"
#include "layers/panels/TextEditorPanel.h"
#include "layers/panels/ViewportPanel.h"

#include <assets/asset_manager/Asset.h>
#include <assets/asset_manager/AssetManager.h>
#include <scene/components/Components.h>
#include <components/extensions/ui_list_layout.h>
#include <scene/systems/Transforms.h>
#include <components/ui_scope.h>
#include <core/ecs/entity_accessor.h>
#include <core/utils/Log.h>
#include <gpu/render_targets/SceneRenderTarget.h>
#include <scene/Scene.h>
#include <scene/instances/DirectionalLight3D.h>
#include <scene/instances/Node3D.h>
#include <core/serialization/SerialDocument.h>
#include <renderer/viewport/ViewportManager.h>
#include <app/Application.h>

static constexpr const char *DOCK_NAME = "Module Editor Dock";
static constexpr const char *VIEWPORT_NAME = "module_editor";
static constexpr const char *ROOT_NAME = "Scene Root";
static constexpr float HOTBAR_BUTTON_WIDTH = 80.0f;
static constexpr float EDITOR_SUN_PITCH = -1.874f;
static constexpr float EDITOR_SUN_INTENSITY = 3.14f;

std::unique_ptr<ModuleEditorWorkspace> ModuleEditorWorkspace::create(Amethyst::TabBar &tabBar,
                                                                              const PanelServices &services,
                                                                              Rapture::AssetHandle handle)
{
    if (!Rapture::AssetManager::getAsset<Rapture::AModule>(handle)) {
        RP_ERROR("asset {} holds no module to open", static_cast<uint64_t>(handle));
        return nullptr;
    }

    auto workspace = std::unique_ptr<ModuleEditorWorkspace>(new ModuleEditorWorkspace(services, handle));
    workspace->build(tabBar);
    return workspace;
}

ModuleEditorWorkspace::ModuleEditorWorkspace(const PanelServices &services, Rapture::AssetHandle handle)
    : Workspace(staticKind()), m_handle(handle)
{
    m_context.services = services;

    m_documentRef = Rapture::AssetManager::getAsset<Rapture::AModule>(handle);
    m_document = m_documentRef ? &m_documentRef.get()->document() : nullptr;
}

ModuleEditorWorkspace::~ModuleEditorWorkspace()
{
    m_panels.clear();

    Rapture::Application::getInstance().getViewportManager().destroyViewport(m_viewport);
    m_viewport = {};

    m_scene.reset();
}

void ModuleEditorWorkspace::build(Amethyst::TabBar &tabBar)
{
    // TODO: follow the asset's name once renaming reaches the workspaces an asset is open in
    setupBase(tabBar, Rapture::AssetManager::getAssetMetadata(m_handle).getName(), Icons::SVG_SCENE);

    m_dockingLayer->name = DOCK_NAME;
    m_dockingLayer->tabBarClasses = {"panel", "panel-tab"};

    setupScene();

    Amethyst::TabBar *propertiesTabBar = nullptr;
    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::TabBar *treeTabBar = nullptr;

    Amethyst::DockScope(*m_dockingLayer)
        .split(
            Amethyst::SplitAxis::VERTICAL, 0.7f,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) {
                r.split(
                    Amethyst::SplitAxis::HORIZONTAL, 0.45f,
                    [&](Amethyst::DockScope &t) { t.panel([&](Amethyst::TabBarScope &tb) { treeTabBar = &tb.component; }); },
                    [&](Amethyst::DockScope &b) {
                        b.panel([&](Amethyst::TabBarScope &tb) { propertiesTabBar = &tb.component; });
                    });
            });

    setupPanels(viewportTabBar, treeTabBar, propertiesTabBar);
    setupHotbar();

    if (Amethyst::LayoutConfig::instance().loadFromFile("layout.conf")) {
        if (auto *entry = Amethyst::LayoutConfig::instance().get(DOCK_NAME)) {
            if (entry->type == Amethyst::ConfigType::DOCK_LAYOUT) {
                m_dockingLayer->applyConfig(entry->dockLayout);
            }
        }
    }
}

void ModuleEditorWorkspace::setupScene()
{
    auto &app = Rapture::Application::getInstance();

    m_scene = std::make_unique<Rapture::Scene>(Rapture::AssetManager::getAssetMetadata(m_handle).getName());
    setupLighting();
    m_sceneRoot = spawn();
    m_scene->active = true;

    Rapture::ecs::EntityAccessor camera = m_scene->createEntity("Editor Camera");
    auto &cameraTransform = camera.add<Rapture::TransformComponent>();
    cameraTransform.local = Rapture::transform::compose(glm::vec3(0.0f, 1.5f, 5.0f), glm::vec3(0.0f), glm::vec3(1.0f));
    cameraTransform.world = cameraTransform.local;
    camera.add<Rapture::CameraComponent>(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);

    auto extent = app.getMainWindow().getSwapChain()->getExtent();
    m_viewport = app.getViewportManager().createViewport({
        .scene = m_scene.get(),
        .camera = camera,
        .name = VIEWPORT_NAME,
        .targetType = Rapture::SceneRenderTarget::TargetType::OFFSCREEN,
        .width = extent.width,
        .height = extent.height,
        .framesInFlight = app.getFramesInFlight(),
        .enableAccelerationStructures = false,
    });
    m_viewport.viewport->createRenderer(Rapture::RendererType::DEFERRED);

    m_context.scene = m_scene.get();
    m_context.viewport = m_viewport.viewport;
}

void ModuleEditorWorkspace::setupLighting()
{
    // parented beside the asset's root rather than inside it, so a save never captures it
    auto *sun = m_scene->root()->add<Rapture::DirectionalLight3D>("Editor Sun");
    sun->setRotation(glm::vec3(EDITOR_SUN_PITCH, 0.0f, 0.0f));
    sun->setColor(glm::vec3(1.0f));
    sun->setIntensity(EDITOR_SUN_INTENSITY);
    sun->setCastsShadow(true);
}

Rapture::SceneObject *ModuleEditorWorkspace::spawn()
{
    // an asset that has never been authored starts from the root everything else hangs off
    if (m_document == nullptr || !m_document->isReadable()) {
        return m_scene->root()->add<Rapture::Node3D>(ROOT_NAME);
    }

    return Rapture::SceneObject::spawnSubtree(*m_scene->root(), m_document->rootView());
}

void ModuleEditorWorkspace::setupPanels(Amethyst::TabBar *viewportTabBar, Amethyst::TabBar *treeTabBar,
                                             Amethyst::TabBar *propertiesTabBar)
{
    if (viewportTabBar != nullptr) {
        m_panels.push_back(std::make_unique<ViewportPanel>(viewportTabBar, m_context));
        m_panels.push_back(std::make_unique<TextEditorPanel>(viewportTabBar, m_context, "Script"));
    }
    if (treeTabBar != nullptr && m_sceneRoot != nullptr) {
        m_panels.push_back(std::make_unique<SceneObjectTreePanel>(treeTabBar, m_context, m_sceneRoot));
    }
    if (propertiesTabBar != nullptr) {
        m_panels.push_back(std::make_unique<PropertiesPanel>(propertiesTabBar, m_context));
    }
}

void ModuleEditorWorkspace::setupHotbar()
{
    if (m_hotbar == nullptr) {
        return;
    }

    auto *layout = m_hotbar->addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    layout->innerPadding = Amethyst::UDim::fromOffset(6.0f);

    Amethyst::UIScope(*m_hotbar).textButton(
        {.classes = {"generic-text-button"},
         .base = {.layoutOrder = 0, .size = Amethyst::UDim2(0.0f, HOTBAR_BUTTON_WIDTH, 1.0f, 0.0f)},
         .label = "Save"},
        [this](Amethyst::TextButtonScope &b) {
            b.component.onMouseButton1ClickCb = [this]() {
                save();
                return Amethyst::EventResult::CONSUMED;
            };
        });
}

void ModuleEditorWorkspace::save()
{
    if (m_document == nullptr || m_sceneRoot == nullptr) {
        return;
    }

    Rapture::SerialDocument authored;
    m_sceneRoot->serialize(authored.root());
    authored.freeze();
    *m_document = std::move(authored);

    auto &project = Rapture::Application::getInstance().getProject();
    if (!Rapture::AssetManager::saveAsset(m_handle, project.getContentDirectory())) {
        RP_ERROR("Could not save module '{}'", Rapture::AssetManager::getAssetMetadata(m_handle).getName());
    }
}

void ModuleEditorWorkspace::onUpdate(float dt)
{
    Workspace::onUpdate(dt);

    if (m_scene != nullptr) {
        m_scene->onUpdate(dt);
    }
}

void ModuleEditorWorkspace::saveLayout()
{
    Amethyst::LayoutConfig::instance().set(m_dockingLayer->name, Amethyst::ConfigEntry(m_dockingLayer->saveConfig()));
}

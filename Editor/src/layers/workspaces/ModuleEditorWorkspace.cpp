#include "ModuleEditorWorkspace.h"

#include "Icons.h"
#include "layers/panels/ModulePropertiesPanel.h"
#include "layers/panels/PropertiesPanel.h"
#include "layers/panels/SceneObjectTreePanel.h"
#include "layers/panels/ViewportPanel.h"

#include <asset_manager/Asset.h>
#include <asset_manager/AssetManager.h>
#include <components/Components.h>
#include <components/extensions/ui_list_layout.h>
#include <components/systems/Transforms.h>
#include <components/ui_scope.h>
#include <logging/Log.h>
#include <modules/puppets/Puppet.h>
#include <render_targets/SceneRenderTarget.h>
#include <scenes/Scene.h>
#include <scenes/entities/Entity.h>
#include <scenes/instances/DirectionalLight3D.h>
#include <scenes/instances/Node3D.h>
#include <viewport/ViewportManager.h>
#include <window_context/Application.h>

static constexpr const char *DOCK_NAME = "Module Editor Dock";
static constexpr const char *VIEWPORT_NAME = "module_editor";
static constexpr const char *ROOT_NAME = "Scene Root";
static constexpr float HOTBAR_BUTTON_WIDTH = 80.0f;
static constexpr float EDITOR_SUN_PITCH = -1.874f;
static constexpr float EDITOR_SUN_INTENSITY = 3.14f;

std::unique_ptr<ModuleEditorWorkspace> ModuleEditorWorkspace::create(Amethyst::TabBar &tabBar, const PanelServices &services,
                                                                     Rapture::AssetHandle handle)
{
    Rapture::AssetRef ref = Rapture::AssetManager::getAsset(handle);
    Rapture::ModuleClass *module = ref ? ref.get()->getUnderlyingAsset<Rapture::ModuleClass>() : nullptr;
    if (module == nullptr) {
        RP_ERROR("asset {} holds no module to open", static_cast<uint64_t>(handle));
        return nullptr;
    }

    if (module->isA<Rapture::Puppet>()) {
        return std::make_unique<PuppetEditorWorkspace>(tabBar, services, handle);
    }

    auto workspace = std::unique_ptr<ModuleEditorWorkspace>(new ModuleEditorWorkspace(services, handle));
    workspace->build(tabBar);
    return workspace;
}

ModuleEditorWorkspace::ModuleEditorWorkspace(const PanelServices &services, Rapture::AssetHandle handle) : m_handle(handle)
{
    m_context.services = services;

    m_moduleRef = Rapture::AssetManager::getAsset(handle);
    m_module = m_moduleRef ? m_moduleRef.get()->getUnderlyingAsset<Rapture::ModuleClass>() : nullptr;
}

ModuleEditorWorkspace::~ModuleEditorWorkspace()
{
    m_panels.clear();

    if (m_viewport != nullptr) {
        Rapture::Application::getInstance().getViewportManager().destroyViewport(VIEWPORT_NAME);
        m_viewport = nullptr;
    }

    m_scene.reset();
}

void ModuleEditorWorkspace::build(Amethyst::TabBar &tabBar)
{
    // TODO: follow the asset's name once renaming reaches the workspaces an asset is open in
    setupBase(tabBar, Rapture::AssetManager::getAssetMetadata(m_handle).getName(), Icons::SVG_MODULE);

    m_dockingLayer->name = DOCK_NAME;
    m_dockingLayer->tabBarClasses = {"panel", "panel-tab"};

    if (usesScene()) {
        setupScene();
    }

    Amethyst::TabBar *propertiesTabBar = nullptr;
    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::TabBar *treeTabBar = nullptr;

    if (m_scene == nullptr) {
        Amethyst::DockScope(*m_dockingLayer).panel([&](Amethyst::TabBarScope &tb) { propertiesTabBar = &tb.component; });
    } else {
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
    }

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
    m_sceneRoot = spawn(*m_scene->root());
    m_scene->active = true;

    Rapture::Entity camera = m_scene->createEntity("Editor Camera");
    auto &cameraTransform = camera.addComponent<Rapture::TransformComponent>();
    cameraTransform.local = Rapture::transform::compose(glm::vec3(0.0f, 1.5f, 5.0f), glm::vec3(0.0f), glm::vec3(1.0f));
    cameraTransform.world = cameraTransform.local;
    camera.addComponent<Rapture::CameraComponent>(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);

    auto extent = app.getMainWindow().getSwapChain()->getExtent();
    m_viewport = app.getViewportManager().createViewport({
        .name = VIEWPORT_NAME,
        .targetType = Rapture::SceneRenderTarget::TargetType::OFFSCREEN,
        .width = extent.width,
        .height = extent.height,
    });
    m_viewport->createRenderer(Rapture::RendererType::DEFERRED);
    m_viewport->setScene(m_scene.get());
    m_viewport->setCamera(camera);

    m_context.scene = m_scene.get();
    m_context.viewport = m_viewport;
}

void ModuleEditorWorkspace::setupLighting()
{
    // parented beside the module's root rather than inside it, so a save never captures it
    auto *sun = m_scene->root()->add<Rapture::DirectionalLight3D>("Editor Sun");
    sun->setRotation(glm::vec3(EDITOR_SUN_PITCH, 0.0f, 0.0f));
    sun->setColor(glm::vec3(1.0f));
    sun->setIntensity(EDITOR_SUN_INTENSITY);
    sun->setCastsShadow(true);
}

void ModuleEditorWorkspace::setupPanels(Amethyst::TabBar *viewportTabBar, Amethyst::TabBar *treeTabBar,
                                        Amethyst::TabBar *propertiesTabBar)
{
    if (viewportTabBar != nullptr) {
        m_panels.push_back(std::make_unique<ViewportPanel>(viewportTabBar, m_context));
    }
    if (treeTabBar != nullptr && m_sceneRoot != nullptr) {
        m_panels.push_back(std::make_unique<SceneObjectTreePanel>(treeTabBar, m_context, m_sceneRoot));
    }
    if (propertiesTabBar != nullptr) {
        auto panel = std::make_unique<ModulePropertiesPanel>(propertiesTabBar, m_context, m_handle);
        m_properties = panel.get();
        m_panels.push_back(std::move(panel));

        if (m_scene != nullptr) {
            m_panels.push_back(std::make_unique<PropertiesPanel>(propertiesTabBar, m_context));
        }
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
    if (m_sceneRoot != nullptr) {
        capture(*m_sceneRoot);
    }

    auto &project = Rapture::Application::getInstance().getProject();
    if (!Rapture::AssetManager::saveAsset(m_handle, project.getContentDirectory())) {
        RP_ERROR("Could not save module '{}'", Rapture::AssetManager::getAssetMetadata(m_handle).getName());
    }
}

Rapture::Instance *ModuleEditorWorkspace::spawn(Rapture::Instance &parent)
{
    (void)parent;
    return nullptr;
}

void ModuleEditorWorkspace::capture(const Rapture::Instance &root)
{
    (void)root;
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

PuppetEditorWorkspace::PuppetEditorWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services, Rapture::AssetHandle handle)
    : ModuleEditorWorkspace(services, handle)
{
    build(tabBar);
}

Rapture::Instance *PuppetEditorWorkspace::spawn(Rapture::Instance &parent)
{
    Rapture::Puppet *puppet = m_module != nullptr ? m_module->as<Rapture::Puppet>() : nullptr;
    if (puppet == nullptr) {
        return nullptr;
    }

    // a puppet that has never been authored starts from the root everything else hangs off
    if (!puppet->hasSceneRoot()) {
        return parent.add<Rapture::Node3D>(ROOT_NAME);
    }

    return puppet->spawn(parent);
}

void PuppetEditorWorkspace::capture(const Rapture::Instance &root)
{
    if (Rapture::Puppet *puppet = m_module != nullptr ? m_module->as<Rapture::Puppet>() : nullptr) {
        puppet->capture(root);
    }
}

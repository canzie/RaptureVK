#include "MaterialEditorWorkspace.h"

#include "Icons.h"
#include "layers/panels/NodeEditorPanel.h"
#include "layers/panels/ViewportPanel.h"

#include <asset_manager/Asset.h>
#include <asset_manager/AssetManager.h>
#include <components/Components.h>
#include <components/extensions/ui_list_layout.h>
#include <components/systems/Transforms.h>
#include <components/ui_scope.h>
#include <materials/MaterialInstance.h>
#include <render_targets/SceneRenderTarget.h>
#include <scenes/Scene.h>
#include <utils/EnginePaths.h>
#include <ecs/entity_accessor.h>
#include <scenes/instances/Environment.h>
#include <viewport/ViewportManager.h>
#include <window_context/Application.h>

static const char *s_previewSceneName = "MaterialPreview";
static const char *s_previewViewportName = "material_preview";

MaterialEditorWorkspace::MaterialEditorWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services,
                                                 Rapture::AssetHandle handle)
    : Workspace(staticKind()), m_handle(handle)
{
    m_context.services = services;

    // TODO: follow the asset's name once renaming reaches the workspaces an asset is open in
    setupBase(tabBar, Rapture::AssetManager::getAssetMetadata(handle).getName(), Icons::SVG_MATERIAL);

    m_dockingLayer->name = "Material Editor Dock";
    m_dockingLayer->tabBarClasses = {"panel-tab"};

    setupPreviewScene();

    Amethyst::TabBar *canvasTabBar = nullptr;
    Amethyst::TabBar *previewTabBar = nullptr;
    Amethyst::DockScope(*m_dockingLayer)
        .split(
            Amethyst::SplitAxis::VERTICAL, 0.7f,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { canvasTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) { r.panel([&](Amethyst::TabBarScope &tb) { previewTabBar = &tb.component; }); });

    if (canvasTabBar != nullptr) {
        auto panel = std::make_unique<NodeEditorPanel>(canvasTabBar, m_context, m_handle);
        m_nodeEditor = panel.get();
        m_materialSelectedConn =
            m_nodeEditor->onMaterialSelectionChanged().connect([this](Rapture::AssetHandle handle) { showMaterialOnSphere(handle); });
        m_panels.push_back(std::move(panel));

        // the panel loaded the material before this connection existed, so the sphere is dressed here
        showMaterialOnSphere(m_handle);
    }

    if (previewTabBar != nullptr) {
        m_panels.push_back(std::make_unique<ViewportPanel>(previewTabBar, m_context));
    }

    setupHotbar();

    if (Amethyst::LayoutConfig::instance().loadFromFile("layout.conf")) {
        if (auto *entry = Amethyst::LayoutConfig::instance().get("Material Editor Dock")) {
            if (entry->type == Amethyst::ConfigType::DOCK_LAYOUT) {
                m_dockingLayer->applyConfig(entry->dockLayout);
            }
        }
    }
}

MaterialEditorWorkspace::~MaterialEditorWorkspace()
{
    m_panels.clear();

    auto &app = Rapture::Application::getInstance();

    app.getViewportManager().destroyViewport(m_previewViewport);
    m_previewViewport = {};

    m_previewScene.reset();
}

void MaterialEditorWorkspace::setupPreviewScene()
{
    auto &app = Rapture::Application::getInstance();

    m_previewScene = std::make_unique<Rapture::Scene>(s_previewSceneName);
    m_previewSphere = m_previewScene->createSphere("Preview Sphere");

    Rapture::ecs::EntityAccessor camera = m_previewScene->createEntity("Preview Camera");
    auto &cameraTransform = camera.add<Rapture::TransformComponent>();
    cameraTransform.local = Rapture::transform::compose(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(1.0f));
    cameraTransform.world = cameraTransform.local;
    camera.add<Rapture::CameraComponent>(60.0f, 16.0f / 9.0f, 0.1f, 100.0f);

    Rapture::ecs::EntityAccessor light = m_previewScene->createEntity("Preview Light");
    auto &lightTransform = light.add<Rapture::TransformComponent>();
    lightTransform.local = Rapture::transform::compose(glm::vec3(0.0f), glm::vec3(-0.6f, 0.5f, 0.0f), glm::vec3(1.0f));
    lightTransform.world = lightTransform.local;
    light.add<Rapture::DirectionalLightComponent>(glm::vec3(1.0f), 3.0f);

    Rapture::Environment *environment = m_previewScene->environment();
    auto skyboxPath = Rapture::EnginePaths::assetDirectory() / "textures/cubemaps/default.cubemap";
    Rapture::AssetRef skyboxRef = Rapture::AssetManager::importAsset(skyboxPath);
    if (environment != nullptr && skyboxRef) {
        environment->setSkybox(skyboxRef.get()->getHandle());
        environment->setSkyIntensity(1.0f);
    }

    m_previewScene->active = true;

    auto extent = app.getMainWindow().getSwapChain()->getExtent();
    m_previewViewport = app.getViewportManager().createViewport({
        .name = s_previewViewportName,
        .targetType = Rapture::SceneRenderTarget::TargetType::OFFSCREEN,
        .width = extent.width,
        .height = extent.height,
        .framesInFlight = app.getFramesInFlight(),
        .enableAccelerationStructures = false,
    });
    m_previewViewport.viewport->createRenderer(Rapture::RendererType::DEFERRED);
    m_previewViewport.viewport->renderSettings().setFlag(Rapture::RENDER_USE_GLOBAL_ILLUMINATION, false);
    m_previewViewport.viewport->setScene(m_previewScene.get());
    m_previewViewport.viewport->setCamera(camera);

    m_context.scene = m_previewScene.get();
    m_context.viewport = m_previewViewport.viewport;

    m_previewScene->locked = true;
}

void MaterialEditorWorkspace::showMaterialOnSphere(Rapture::AssetHandle handle)
{
    if (!m_previewSphere.isValid()) {
        return;
    }

    Rapture::AssetRef ref = Rapture::AssetManager::getAsset(handle);
    if (!ref) {
        return;
    }

    if (!m_previewSphere.has<Rapture::MaterialComponent>()) {
        m_previewSphere.add<Rapture::MaterialComponent>(std::move(ref));
        return;
    }
    m_previewSphere.write<Rapture::MaterialComponent>()->material = Rapture::AssetPtr<Rapture::MaterialInstance>(std::move(ref));
}

void MaterialEditorWorkspace::setupHotbar()
{
    if (m_hotbar == nullptr) {
        return;
    }

    auto *layout = m_hotbar->addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    layout->innerPadding = Amethyst::UDim::fromOffset(6.0f);

    Amethyst::UIScope(*m_hotbar).textButton(
        {.base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(80.0f, 28.0f)},
         .text = {.textXAlignment = Amethyst::TextXAlignment::CENTER, .textYAlignment = Amethyst::TextYAlignment::CENTER},
         .label = "Compile"},
        [this](Amethyst::TextButtonScope &b) {
            b.component.onMouseButton1ClickCb = [this]() {
                if (m_nodeEditor != nullptr) {
                    m_nodeEditor->compileGraph();
                }
                return Amethyst::EventResult::CONSUMED;
            };
        });
}

void MaterialEditorWorkspace::onUpdate(float dt)
{
    Workspace::onUpdate(dt);

    if (m_previewScene != nullptr) {
        m_previewScene->onUpdate(dt);
    }
}

void MaterialEditorWorkspace::saveLayout()
{
    Amethyst::LayoutConfig::instance().set(m_dockingLayer->name, Amethyst::ConfigEntry(m_dockingLayer->saveConfig()));
}

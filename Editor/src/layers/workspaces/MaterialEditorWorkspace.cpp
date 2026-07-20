#include "MaterialEditorWorkspace.h"

#include "layers/panels/NodeEditorPanel.h"
#include "layers/panels/ViewportPanel.h"

#include <asset_manager/Asset.h>
#include <asset_manager/AssetManager.h>
#include <components/Components.h>
#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>
#include <materials/MaterialInstance.h>
#include <render_targets/SceneRenderTarget.h>
#include <scenes/Scene.h>
#include <scenes/entities/Entity.h>
#include <viewport/ViewportManager.h>
#include <window_context/Application.h>

static const char *s_previewSceneName = "MaterialPreview";
static const char *s_previewViewportName = "material_preview";

MaterialEditorWorkspace::MaterialEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services)
{
    m_context.services = services;
    setupBase(tabs, "Material Editor");
    m_dockingLayer->name = "Material Editor Dock";

    setupPreviewScene();

    Amethyst::TabBar *canvasTabBar = nullptr;
    Amethyst::TabBar *previewTabBar = nullptr;
    Amethyst::DockScope(*m_dockingLayer)
        .split(
            Amethyst::SplitAxis::VERTICAL, 0.7f,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { canvasTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) { r.panel([&](Amethyst::TabBarScope &tb) { previewTabBar = &tb.component; }); });

    if (canvasTabBar != nullptr) {
        canvasTabBar->addClass("panel-tab-bar");
        auto panel = std::make_unique<NodeEditorPanel>(canvasTabBar, m_context);
        m_nodeEditor = panel.get();
        m_materialSelectedConn =
            m_nodeEditor->onMaterialSelectionChanged().connect([this](Rapture::AssetHandle handle) { showMaterialOnSphere(handle); });
        m_panels.push_back(std::move(panel));
    }

    if (previewTabBar != nullptr) {
        previewTabBar->addClass("panel-tab-bar");
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

    if (m_previewViewport != nullptr) {
        app.getViewportManager().destroyViewport(s_previewViewportName);
        m_previewViewport = nullptr;
    }

    if (m_previewScene != nullptr) {
        app.getProject().getSceneManager().destroyScene(s_previewSceneName);
        m_previewScene = nullptr;
    }
}

void MaterialEditorWorkspace::setupPreviewScene()
{
    auto &app = Rapture::Application::getInstance();
    auto &sceneManager = app.getProject().getSceneManager();

    m_previewScene = sceneManager.createScene(s_previewSceneName);
    m_previewSphere = m_previewScene->createSphere("Preview Sphere");

    Rapture::Entity camera = m_previewScene->createEntity("Preview Camera");
    camera.addComponent<Rapture::TransformComponent>(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(1.0f));
    auto &cameraComp = camera.addComponent<Rapture::CameraComponent>(60.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    cameraComp.isMainCamera = true;
    m_previewScene->setMainCamera(camera);

    Rapture::Entity light = m_previewScene->createEntity("Preview Light");
    light.addComponent<Rapture::TransformComponent>(glm::vec3(0.0f), glm::vec3(-0.6f, 0.5f, 0.0f), glm::vec3(1.0f));
    light.addComponent<Rapture::DirectionalLightComponent>(glm::vec3(1.0f), 3.0f);

    Rapture::Entity environment = m_previewScene->environmentEntity();
    auto skyboxPath = app.getProject().getProjectRootDirectory() / "assets/textures/cubemaps/default.cubemap";
    environment.addComponent<Rapture::SkyboxComponent>(skyboxPath, 1.0f);

    sceneManager.activateScene(m_previewScene);

    auto extent = app.getMainWindow().getSwapChain()->getExtent();
    m_previewViewport = app.getViewportManager().createViewport({
        .name = s_previewViewportName,
        .targetType = Rapture::SceneRenderTarget::TargetType::OFFSCREEN,
        .width = extent.width,
        .height = extent.height,
    });
    m_previewViewport->createRenderer(Rapture::RendererType::DEFERRED);
    m_previewViewport->renderSettings().setFlag(Rapture::RENDER_USE_GLOBAL_ILLUMINATION, false);
    m_previewViewport->setScene(m_previewScene);
    m_previewViewport->setCamera(camera);

    m_context.scene = m_previewScene;
    m_context.viewport = m_previewViewport;

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

    auto *materialComp = m_previewSphere.tryGetComponent<Rapture::MaterialComponent>();
    if (materialComp == nullptr) {
        m_previewSphere.addComponent<Rapture::MaterialComponent>(std::move(ref));
        return;
    }
    materialComp->material = Rapture::AssetPtr<Rapture::MaterialInstance>(std::move(ref));
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

void MaterialEditorWorkspace::saveLayout()
{
    Amethyst::LayoutConfig::instance().set(m_dockingLayer->name, Amethyst::ConfigEntry(m_dockingLayer->saveConfig()));
}

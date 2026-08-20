#include "AssetPreviewWorkspace.h"

#include <app/Application.h>
#include <assets/asset_manager/AssetManager.h>
#include <gpu/render_targets/SceneRenderTarget.h>
#include <renderer/viewport/ViewportManager.h>
#include <scene/Scene.h>
#include <scene/instances/controllers/CameraController.h>

static constexpr uint32_t PREVIEW_VIEWPORT_WIDTH = 1280;
static constexpr uint32_t PREVIEW_VIEWPORT_HEIGHT = 720;

static const glm::vec3 FOCUS_DIRECTION = glm::normalize(glm::vec3(0.55f, -0.35f, -0.75f));

// what Scene::addDefaultContent names the floor it adds, scuffed but works for now
static constexpr std::string_view DEFAULT_FLOOR_NAME = "Floor";

AssetPreviewWorkspace::AssetPreviewWorkspace(std::string_view kind, const PanelServices &services,
                                            Rapture::AssetHandle handle)
    : Workspace(kind), m_handle(handle)
{
    m_context.services = services;
}

AssetPreviewWorkspace::~AssetPreviewWorkspace()
{
    m_panels.clear();

    Rapture::Application::getInstance().getViewportManager().destroyViewport(m_previewViewport);
    m_previewViewport = {};

    m_previewScene.reset();
}

std::string_view AssetPreviewWorkspace::assetName() const
{
    return Rapture::AssetManager::getAssetMetadata(m_handle).getName();
}

void AssetPreviewWorkspace::setupPreviewScene(std::string_view sceneName)
{
    auto &app = Rapture::Application::getInstance();

    m_previewScene = std::make_unique<Rapture::Scene>(std::string(sceneName));
    m_previewScene->addDefaultContent();
    m_previewScene->active = true;

    Rapture::SceneObject *floor = m_previewScene->root()->findChild(DEFAULT_FLOOR_NAME);
    Rapture::Mesh3D *floorMesh = floor != nullptr ? floor->as<Rapture::Mesh3D>() : nullptr;
    if (floorMesh != nullptr) {
        floorMesh->setMaterial(Rapture::RE_GRID_MATERIAL_INSTANCE);
    }

    m_previewViewport = app.getViewportManager().createViewport({
        .name = std::string(sceneName),
        .targetType = Rapture::SceneRenderTarget::TargetType::OFFSCREEN,
        .width = PREVIEW_VIEWPORT_WIDTH,
        .height = PREVIEW_VIEWPORT_HEIGHT,
        .framesInFlight = app.getFramesInFlight(),
        .enableAccelerationStructures = false,
    });
    m_previewViewport.viewport->createRenderer(Rapture::RendererType::DEFERRED);
    m_previewViewport.viewport->renderSettings().setFlag(Rapture::RENDER_USE_GLOBAL_ILLUMINATION, false);
    m_previewViewport.viewport->setScene(m_previewScene.get());

    m_context.scene = m_previewScene.get();
    m_context.viewport = m_previewViewport.viewport;
}

void AssetPreviewWorkspace::setupDockingLayer()
{
    m_dockingLayer->name = std::string(assetName());
    m_dockingLayer->tabBarClasses = {"panel", "panel-tab"};
}

void AssetPreviewWorkspace::applyStoredLayout()
{
    if (!Amethyst::LayoutConfig::instance().loadFromFile("layout.conf")) {
        return;
    }

    auto *entry = Amethyst::LayoutConfig::instance().get(m_dockingLayer->name);
    if (entry == nullptr || entry->type != Amethyst::ConfigType::DOCK_LAYOUT) {
        return;
    }

    m_dockingLayer->applyConfig(entry->dockLayout);
}

void AssetPreviewWorkspace::setFocusBounds(const glm::vec3 &min, const glm::vec3 &max)
{
    m_focusCenter = (min + max) * 0.5f;
    m_focusRadius = glm::length(max - min) * 0.5f;
}

void AssetPreviewWorkspace::frameFocusBounds()
{
    if (m_previewViewport.viewport == nullptr) {
        return;
    }

    Rapture::CameraController *controller = m_previewViewport.viewport->editorBinding().controller;
    if (controller == nullptr) {
        return;
    }

    controller->focusOn(m_focusCenter, m_focusRadius, FOCUS_DIRECTION);
    m_framePending = false;
}

void AssetPreviewWorkspace::onUpdate(float dt)
{
    Workspace::onUpdate(dt);

    // the camera is handed to the viewport a frame after it is created, so this cannot be done on open
    if (m_framePending) {
        frameFocusBounds();
    }

    if (m_previewScene != nullptr) {
        m_previewScene->onUpdate(dt);
    }
}

void AssetPreviewWorkspace::saveLayout()
{
    Amethyst::LayoutConfig::instance().set(m_dockingLayer->name, Amethyst::ConfigEntry(m_dockingLayer->saveConfig()));
}

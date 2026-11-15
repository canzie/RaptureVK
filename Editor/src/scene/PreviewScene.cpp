#include "PreviewScene.h"

#include <rapture/app/Application.h>
#include <rapture/assets/asset_manager/ReservedAssets.h>
#include <rapture/gpu/render_targets/SceneRenderTarget.h>
#include <rapture/renderer/viewport/ViewportManager.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/DirectionalLight3D.h>
#include <rapture/scene/instances/Environment.h>
#include <rapture/scene/instances/StaticMesh3D.h>
#include <rapture/scene/instances/controllers/CameraController.h>

// the panel showing it resizes it to its pane, so this only has to be a sane first frame
static constexpr uint32_t PREVIEW_WIDTH = 1280;
static constexpr uint32_t PREVIEW_HEIGHT = 720;

static const glm::vec3 FOCUS_DIRECTION = glm::normalize(glm::vec3(0.55f, -0.35f, -0.75f));

static constexpr float SUN_PITCH = -1.874f;
static constexpr float SUN_INTENSITY = 3.14f;
static constexpr float SKY_INTENSITY = 0.1f;
static constexpr float GROUND_SIZE = 20.0f;

PreviewScene::PreviewScene(std::string_view name) : m_name(name)
{
    m_scene = std::make_unique<Rapture::Scene>(m_name);
    m_scene->active = true;
}

PreviewScene::~PreviewScene()
{
    Rapture::Application::getInstance().getViewportManager().destroyViewport(m_viewport);
    m_viewport = {};
    m_scene.reset();
}

void PreviewScene::addGroundAndSky()
{
    auto *sun = m_scene->root()->add<Rapture::DirectionalLight3D>("Sun");
    sun->setRotation(glm::vec3(SUN_PITCH, 0.0f, 0.0f));
    sun->setColor(glm::vec3(1.0f));
    sun->setIntensity(SUN_INTENSITY);
    sun->setAtmosphereSun(true);
    sun->setCastsShadow(true);

    auto *ground = m_scene->root()->add<Rapture::StaticMesh3D>("Ground");
    ground->setMesh(Rapture::RE_PRIMITIVE_PLANE_MESH);
    ground->setMaterial(0, Rapture::RE_GRID_MATERIAL_INSTANCE);
    ground->setScale(glm::vec3(GROUND_SIZE, 1.0f, GROUND_SIZE));

    Rapture::Environment *environment = m_scene->environment();
    if (environment != nullptr) {
        environment->setUsesAtmosphereSkybox(true);
        environment->setSkyIntensity(SKY_INTENSITY);
    }
}

Rapture::Viewport *PreviewScene::createViewport(Rapture::ecs::EntityAccessor camera)
{
    auto &app = Rapture::Application::getInstance();

    m_viewport = app.getViewportManager().createViewport({
        .scene = m_scene.get(),
        .camera = camera,
        .name = m_name,
        .targetType = Rapture::SceneRenderTarget::TargetType::OFFSCREEN,
        .width = PREVIEW_WIDTH,
        .height = PREVIEW_HEIGHT,
        .framesInFlight = app.getFramesInFlight(),
        .enableAccelerationStructures = false,
    });

    m_viewport.viewport->createRenderer(Rapture::RendererType::DEFERRED);
    m_viewport.viewport->renderSettings().setFlag(Rapture::RENDER_USE_GLOBAL_ILLUMINATION, false);

    return m_viewport.viewport;
}

void PreviewScene::focusOn(const glm::vec3 &min, const glm::vec3 &max)
{
    if (m_viewport.viewport == nullptr) {
        return;
    }

    Rapture::CameraController *controller = m_viewport.viewport->editorBinding().controller;
    if (controller == nullptr) {
        return;
    }

    const glm::vec3 center = (min + max) * 0.5f;
    controller->focusOn(center, glm::length(max - min) * 0.5f, FOCUS_DIRECTION);
}

glm::vec3 PreviewScene::centreMeshOnOrigin(std::span<Rapture::Mesh3D *const> meshes)
{
    if (meshes.empty()) {
        return glm::vec3(0.0f);
    }

    glm::vec3 min(0.0f);
    glm::vec3 max(0.0f);
    for (Rapture::Mesh3D *mesh : meshes) {
        min = glm::min(min, mesh->boundsMin());
        max = glm::max(max, mesh->boundsMax());
    }

    // authored geometry can sit anywhere, and the scene around it is built at the origin
    const glm::vec3 offset = -(min + max) * 0.5f;
    for (Rapture::Mesh3D *mesh : meshes) {
        mesh->setPosition(offset);
    }

    focusOn(min + offset, max + offset);
    return offset;
}

void PreviewScene::onUpdate(float dt)
{
    m_scene->onUpdate(dt);
}

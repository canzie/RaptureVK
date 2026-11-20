#include "PreviewScene.h"

#include "scene/EditorCamera.h"

#include <rapture/app/Application.h>
#include <rapture/assets/asset_manager/ReservedAssets.h>
#include <rapture/gpu/render_targets/SceneRenderTarget.h>
#include <rapture/renderer/viewport/ViewportManager.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Camera3D.h>
#include <rapture/scene/instances/DirectionalLight3D.h>
#include <rapture/scene/instances/Environment.h>
#include <rapture/scene/instances/StaticMesh3D.h>
#include <rapture/scene/instances/controllers/CameraController.h>

#include <algorithm>

// the panel showing it resizes it to its pane, so this only has to be a sane first frame
static constexpr uint32_t PREVIEW_WIDTH = 1280;
static constexpr uint32_t PREVIEW_HEIGHT = 720;

static const glm::vec3 FOCUS_DIRECTION = glm::normalize(glm::vec3(0.55f, -0.35f, -0.75f));

static constexpr float SUN_PITCH = -1.874f;
static constexpr float SUN_INTENSITY = 3.14f;
static constexpr float SKY_INTENSITY = 0.1f;
static constexpr float GROUND_SIZE = 20.0f;

static constexpr float NEAR_PLANE_PER_RADIUS = 0.01f;
static constexpr float FAR_PLANE_PER_RADIUS = 100.0f;
static constexpr float SHADOW_DISTANCE_PER_RADIUS = 4.0f;
static constexpr float SMALLEST_FRAMED_RADIUS = 0.01f;

PreviewScene::PreviewScene(std::string_view name) : m_name(name)
{
    m_scene = std::make_unique<Rapture::Scene>(m_name);
    m_scene->active = true;
}

PreviewScene::~PreviewScene()
{
    Rapture::Application::getInstance().getViewportManager().destroyViewport(m_viewport);
    m_viewport = {};
    m_editorCamera.reset();
    m_scene.reset();
}

void PreviewScene::addGroundAndSky()
{
    addSky();

    auto *ground = m_scene->root()->add<Rapture::StaticMesh3D>("Ground");
    ground->setMesh(Rapture::RE_PRIMITIVE_PLANE_MESH);
    ground->setMaterial(0, Rapture::RE_GRID_MATERIAL_INSTANCE);
    ground->setScale(glm::vec3(GROUND_SIZE, 1.0f, GROUND_SIZE));
}

void PreviewScene::addSky()
{
    m_sun = m_scene->root()->add<Rapture::DirectionalLight3D>("Sun");
    m_sun->setRotation(glm::vec3(SUN_PITCH, 0.0f, 0.0f));
    m_sun->setColor(glm::vec3(1.0f));
    m_sun->setIntensity(SUN_INTENSITY);
    m_sun->setAtmosphereSun(true);
    m_sun->setCastsShadow(true);

    Rapture::Environment *environment = m_scene->environment();
    if (environment != nullptr) {
        environment->setUsesAtmosphereSkybox(true);
        environment->setSkyIntensity(SKY_INTENSITY);
    }
}

Rapture::Viewport *PreviewScene::createViewport(Rapture::ecs::EntityAccessor camera)
{
    auto &app = Rapture::Application::getInstance();

    if (!camera.isValid()) {
        m_editorCamera = std::make_unique<EditorCamera>(*m_scene);
        camera = m_editorCamera->camera().accessor();
    }

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

static float s_framedRadius(const glm::vec3 &min, const glm::vec3 &max)
{
    return std::max(glm::length(max - min) * 0.5f, SMALLEST_FRAMED_RADIUS);
}

void PreviewScene::fitViewRangeTo(const glm::vec3 &min, const glm::vec3 &max)
{
    if (m_editorCamera == nullptr) {
        return;
    }

    const float radius = s_framedRadius(min, max);

    Rapture::Camera3D &camera = m_editorCamera->camera();
    camera.setNearPlane(radius * NEAR_PLANE_PER_RADIUS);
    camera.setFarPlane(radius * FAR_PLANE_PER_RADIUS);

    if (m_sun != nullptr) {
        m_sun->setShadowDistance(radius * SHADOW_DISTANCE_PER_RADIUS);
    }
}

void PreviewScene::focusOn(const glm::vec3 &min, const glm::vec3 &max)
{
    if (m_editorCamera == nullptr) {
        return;
    }

    fitViewRangeTo(min, max);
    m_editorCamera->controller().focusOn((min + max) * 0.5f, s_framedRadius(min, max), FOCUS_DIRECTION);
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

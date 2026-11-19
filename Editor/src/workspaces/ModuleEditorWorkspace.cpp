#include "ModuleEditorWorkspace.h"

#include "Icons.h"
#include "panels/PropertiesPanel.h"
#include "panels/SceneObjectTreePanel.h"
#include "panels/TextEditorPanel.h"
#include "panels/ViewportPanel.h"

#include <rapture/app/Application.h>
#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/core/ecs/entity_accessor.h>
#include <rapture/core/serialization/SerialDocument.h>
#include <rapture/core/utils/Log.h>
#include <rapture/gpu/render_targets/SceneRenderTarget.h>
#include <rapture/renderer/viewport/ViewportManager.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/components/Components.h>
#include <rapture/scene/instances/DirectionalLight3D.h>
#include <rapture/scene/instances/Node3D.h>
#include <rapture/scene/systems/Transforms.h>

static constexpr std::string_view ROOT_NAME = "Scene Root";

// the viewport panel resizes it to its pane, so this only has to be a sane first frame
static constexpr uint32_t VIEWPORT_WIDTH = 1280;
static constexpr uint32_t VIEWPORT_HEIGHT = 720;

static constexpr float SUN_PITCH = -1.874f;
static constexpr float SUN_INTENSITY = 3.14f;

static constexpr float TREE_SPLIT = 0.7f;
static constexpr float PROPERTIES_SPLIT = 0.45f;

ModuleEditorWorkspace::ModuleEditorWorkspace(const EditorContext &context, Rapture::AssetHandle handle)
    : AssetWorkspace(context, handle)
{
    m_documentRef = Rapture::AssetManager::getAsset<Rapture::AModule>(handle);
    m_document = m_documentRef ? &m_documentRef.get()->document() : nullptr;
    if (m_document == nullptr) {
        RP_ERROR("asset {} holds no module, opening an empty one", static_cast<uint64_t>(handle));
    }

    setupScene();
}

ModuleEditorWorkspace::~ModuleEditorWorkspace()
{
    destroyPanels();

    Rapture::Application::getInstance().getViewportManager().destroyViewport(m_viewport);
    m_viewport = {};

    m_scene.reset();
}

std::string_view ModuleEditorWorkspace::tabIcon() const
{
    return Icons::SVG_SCENE;
}

void ModuleEditorWorkspace::setupScene()
{
    auto &app = Rapture::Application::getInstance();

    m_scene = std::make_unique<Rapture::Scene>(std::string(assetName()));
    setupLighting();
    m_sceneRoot = spawn();
    m_scene->active = true;

    Rapture::ecs::EntityAccessor camera = m_scene->createEntity("Editor Camera");
    auto &cameraTransform = camera.add<Rapture::TransformComponent>();
    cameraTransform.local = Rapture::transform::compose(glm::vec3(0.0f, 1.5f, 5.0f), glm::vec3(0.0f), glm::vec3(1.0f));
    cameraTransform.world = cameraTransform.local;
    camera.add<Rapture::CameraComponent>(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);

    m_viewport = app.getViewportManager().createViewport({
        .scene = m_scene.get(),
        .camera = camera,
        .name = std::string(assetName()),
        .targetType = Rapture::SceneRenderTarget::TargetType::OFFSCREEN,
        .width = VIEWPORT_WIDTH,
        .height = VIEWPORT_HEIGHT,
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
    sun->setRotation(glm::vec3(SUN_PITCH, 0.0f, 0.0f));
    sun->setColor(glm::vec3(1.0f));
    sun->setIntensity(SUN_INTENSITY);
    sun->setCastsShadow(true);
}

Rapture::SceneObject *ModuleEditorWorkspace::spawn()
{
    // an asset that has never been authored starts from the root everything else hangs off
    if (m_document == nullptr || !m_document->isReadable()) {
        return m_scene->root()->add<Rapture::Node3D>(std::string(ROOT_NAME));
    }

    return Rapture::SceneObject::spawnSubtree(*m_scene->root(), m_document->rootView());
}

void ModuleEditorWorkspace::build()
{
    Amethyst::TabBar *propertiesTabBar = nullptr;
    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::TabBar *treeTabBar = nullptr;

    Amethyst::DockScope(*m_contentDock)
        .split(
            Amethyst::SplitAxis::VERTICAL, TREE_SPLIT,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) {
                r.split(
                    Amethyst::SplitAxis::HORIZONTAL, PROPERTIES_SPLIT,
                    [&](Amethyst::DockScope &t) { t.panel([&](Amethyst::TabBarScope &tb) { treeTabBar = &tb.component; }); },
                    [&](Amethyst::DockScope &b) { b.panel([&](Amethyst::TabBarScope &tb) { propertiesTabBar = &tb.component; }); });
            });

    addPanel(std::make_unique<ViewportPanel>(m_context, ViewportOverlays{.toolbar = true, .orientationGizmo = true}), viewportTabBar);
    addPanel(std::make_unique<TextEditorPanel>(m_context, "Script"), viewportTabBar);

    if (m_sceneRoot != nullptr) {
        addPanel(std::make_unique<SceneObjectTreePanel>(m_context, m_sceneRoot), treeTabBar);
    }
    addPanel(std::make_unique<PropertiesPanel>(m_context), propertiesTabBar);
}

void ModuleEditorWorkspace::buildHotbar()
{
    addHotbarButton("Save", [this]() { save(); });
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
    if (!Rapture::AssetManager::saveAsset(handle(), project.getContentDirectory())) {
        RP_ERROR("Could not save module '{}'", assetName());
    }
}

void ModuleEditorWorkspace::onUpdate(float dt)
{
    Workspace::onUpdate(dt);

    if (isActive()) {
        m_scene->onUpdate(dt);
    }
}

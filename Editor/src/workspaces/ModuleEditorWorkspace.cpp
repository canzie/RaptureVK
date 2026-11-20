#include "ModuleEditorWorkspace.h"

#include "Icons.h"
#include "panels/AddSceneObjectMenu.h"
#include "panels/PropertiesPanel.h"
#include "panels/SceneObjectTreePanel.h"
#include "panels/ViewportPanel.h"
#include "scene/EditorCamera.h"
#include "ui/AssetVisuals.h"
#include "ui/controls/IconLabelButton.h"
#include "ui/menus/ContextMenuItems.h"

#include <rapture/app/Application.h>
#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/core/serialization/SerialDocument.h>
#include <rapture/core/utils/Log.h>
#include <rapture/gpu/render_targets/SceneRenderTarget.h>
#include <rapture/renderer/viewport/ViewportManager.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Camera3D.h>
#include <rapture/scene/instances/DirectionalLight3D.h>
#include <rapture/scene/instances/Node3D.h>

static constexpr std::string_view ROOT_NAME = "Scene Root";
static constexpr std::string_view ADD_BUTTON_CLASS = "hotbar-primary-button";

// the viewport panel resizes it to its pane, so this only has to be a sane first frame
static constexpr uint32_t VIEWPORT_WIDTH = 1280;
static constexpr uint32_t VIEWPORT_HEIGHT = 720;

static constexpr float SUN_PITCH = -1.874f;
static constexpr float SUN_INTENSITY = 3.14f;

static constexpr float VIEWPORT_SPLIT = 0.68f;
static constexpr float TREE_SPLIT = 0.4f;

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

    m_editorCamera.reset();
    m_scene.reset();
}

std::string_view ModuleEditorWorkspace::tabIcon() const
{
    return Asset_iconForType(Rapture::ASSET_MODULE);
}

void ModuleEditorWorkspace::setupScene()
{
    auto &app = Rapture::Application::getInstance();

    m_scene = std::make_unique<Rapture::Scene>(std::string(assetName()));
    m_scene->active = true;

    // a sibling of the module's root rather than a child of it, so a save never captures it
    auto *sun = m_scene->root()->add<Rapture::DirectionalLight3D>("Editor Sun");
    sun->setRotation(glm::vec3(SUN_PITCH, 0.0f, 0.0f));
    sun->setColor(glm::vec3(1.0f));
    sun->setIntensity(SUN_INTENSITY);
    sun->setCastsShadow(true);

    m_moduleRoot = spawnModuleRoot();

    m_editorCamera = std::make_unique<EditorCamera>(*m_scene);

    m_viewport = app.getViewportManager().createViewport({
        .scene = m_scene.get(),
        .camera = m_editorCamera->camera().accessor(),
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
    m_context.editorCamera = m_editorCamera.get();
}

Rapture::SceneObject *ModuleEditorWorkspace::spawnModuleRoot()
{
    if (m_document == nullptr || !m_document->isReadable()) {
        return m_scene->root()->add<Rapture::Node3D>(std::string(ROOT_NAME));
    }

    return Rapture::SceneObject::spawnSubtree(*m_scene->root(), m_document->rootView());
}

void ModuleEditorWorkspace::build()
{
    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::TabBar *treeTabBar = nullptr;
    Amethyst::TabBar *propertiesTabBar = nullptr;

    Amethyst::DockScope(*m_contentDock)
        .split(
            Amethyst::SplitAxis::VERTICAL, VIEWPORT_SPLIT,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) {
                r.split(
                    Amethyst::SplitAxis::HORIZONTAL, TREE_SPLIT,
                    [&](Amethyst::DockScope &t) { t.panel([&](Amethyst::TabBarScope &tb) { treeTabBar = &tb.component; }); },
                    [&](Amethyst::DockScope &b) { b.panel([&](Amethyst::TabBarScope &tb) { propertiesTabBar = &tb.component; }); });
            });

    addPanel(std::make_unique<ViewportPanel>(m_context, ViewportOverlays{.toolbar = true, .orientationGizmo = true}), viewportTabBar);
    if (m_moduleRoot != nullptr) {
        addPanel(std::make_unique<SceneObjectTreePanel>(m_context, m_moduleRoot), treeTabBar);
    }
    addPanel(std::make_unique<PropertiesPanel>(m_context), propertiesTabBar);

    m_addMenu = m_container->add<Amethyst::ContextMenu>();
    m_addMenu->setRowFactories({.separator = [] { return std::make_unique<SeparatorMenuItemView>(); }});
}

void ModuleEditorWorkspace::buildHotbar()
{
    m_addButton = addHotbarButton("Add", Icons::SVG_PLUS, ADD_BUTTON_CLASS, [this]() { showAddMenu(); });
    addHotbarSeparator();
    addHotbarButton("Save", Icons::SVG_SAVE, [this]() { save(); });
    addHotbarSeparator();
}

Rapture::SceneObject *ModuleEditorWorkspace::getAddTarget() const
{
    Rapture::ecs::EntityAccessor selected = m_selection.entity();
    if (selected.isValid()) {
        if (Rapture::SceneObject *object = m_scene->instanceFor(selected.getEntity())) {
            return object;
        }
    }
    return m_moduleRoot;
}

void ModuleEditorWorkspace::showAddMenu()
{
    Rapture::SceneObject *target = getAddTarget();
    if (m_addMenu == nullptr || target == nullptr) {
        return;
    }

    m_addMenu->setItems(AddSceneObjectMenu_buildItems(target, m_selection, SCENE_OBJECT_SCOPE_ASSET));
    m_addMenu->showAt({m_addButton->absolutePosition.x, m_addButton->absolutePosition.y + m_addButton->absoluteSize.y});
}

void ModuleEditorWorkspace::save()
{
    if (m_document == nullptr || m_moduleRoot == nullptr) {
        return;
    }

    Rapture::SerialDocument authored;
    m_moduleRoot->serialize(authored.root());
    authored.freeze();
    *m_document = std::move(authored);

    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(handle());
    if (!Rapture::AssetManager::saveAsset(handle(), metadata.assetPath.parent_path())) {
        RP_ERROR("could not save module '{}'", assetName());
    }
}

void ModuleEditorWorkspace::onUpdate(float dt)
{
    Workspace::onUpdate(dt);

    if (isActive()) {
        m_scene->onUpdate(dt);
    }
}

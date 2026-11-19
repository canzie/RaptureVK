#include "MaterialEditorWorkspace.h"

#include "Icons.h"
#include "panels/NodeEditorPanel.h"
#include "panels/ViewportPanel.h"
#include "ui/ControlTooltip.h"

#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/assets/asset_manager/ReservedAssets.h>
#include <rapture/core/utils/EnginePaths.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Camera3D.h>
#include <rapture/scene/instances/DirectionalLight3D.h>
#include <rapture/scene/instances/Environment.h>
#include <rapture/scene/instances/SceneObject.h>
#include <rapture/scene/instances/StaticMesh3D.h>

#include <array>

enum MaterialEditorTooltip {
    MATERIAL_EDITOR_TOOLTIP_COMPILE,
    MATERIAL_EDITOR_TOOLTIP_COUNT
};

static constexpr std::array<ControlTooltip, MATERIAL_EDITOR_TOOLTIP_COUNT> MATERIAL_EDITOR_TOOLTIPS = {{
    {.name = "Compile", .description = "Builds the material's shader from its graph"},
}};

static constexpr std::string_view PREVIEW_SCENE_NAME = "MaterialPreview";

static constexpr float PREVIEW_SPLIT = 0.7f;

static constexpr float SKY_INTENSITY = 1.0f;
static constexpr float SUN_INTENSITY = 3.0f;
static constexpr glm::vec3 LIGHT_ROTATION = glm::vec3(-0.6f, 0.5f, 0.0f);

static constexpr glm::vec3 CAMERA_POSITION = glm::vec3(0.0f, 0.0f, 4.0f);
static constexpr float CAMERA_FIELD_OF_VIEW = 60.0f;
static constexpr float CAMERA_NEAR_PLANE = 0.1f;
static constexpr float CAMERA_FAR_PLANE = 100.0f;

MaterialEditorWorkspace::MaterialEditorWorkspace(const EditorContext &context, Rapture::AssetHandle handle)
    : PreviewWorkspace(context, handle, PREVIEW_SCENE_NAME)
{
    Rapture::Scene *scene = preview().scene();
    Rapture::SceneObject *root = scene->root();

    m_previewSphere = root->add<Rapture::StaticMesh3D>("Preview Sphere");
    m_previewSphere->setMesh(Rapture::RE_PRIMITIVE_SPHERE_MESH);

    auto *camera = root->add<Rapture::Camera3D>("Preview Camera");
    camera->setPosition(CAMERA_POSITION);
    camera->setFieldOfView(CAMERA_FIELD_OF_VIEW);
    camera->setNearPlane(CAMERA_NEAR_PLANE);
    camera->setFarPlane(CAMERA_FAR_PLANE);

    auto *light = root->add<Rapture::DirectionalLight3D>("Preview Light");
    light->setRotation(LIGHT_ROTATION);
    light->setColor(glm::vec3(1.0f));
    light->setIntensity(SUN_INTENSITY);

    Rapture::Environment *environment = scene->environment();
    // TODO: make this a reserved asset, so opening a material never reaches the disk
    m_skybox = Rapture::AssetManager::importAsset(Rapture::EnginePaths::assetDirectory() / "textures/cubemaps/default.cubemap");
    if (environment != nullptr && m_skybox) {
        environment->setSkybox(m_skybox->handle());
        environment->setSkyIntensity(SKY_INTENSITY);
    }

    m_context.viewport = preview().createViewport(camera->accessor());

    scene->locked = true;
}

std::string_view MaterialEditorWorkspace::tabIcon() const
{
    return Icons::SVG_MATERIAL;
}

void MaterialEditorWorkspace::build()
{
    Amethyst::TabBar *canvasTabBar = nullptr;
    Amethyst::TabBar *previewTabBar = nullptr;
    Amethyst::DockScope(*m_contentDock)
        .split(
            Amethyst::SplitAxis::VERTICAL, PREVIEW_SPLIT,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { canvasTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) { r.panel([&](Amethyst::TabBarScope &tb) { previewTabBar = &tb.component; }); });

    m_nodeEditor = addPanel(std::make_unique<NodeEditorPanel>(m_context, handle()), canvasTabBar);
    m_materialSelectedConn =
        m_nodeEditor->onMaterialSelectionChanged().connect([this](Rapture::AssetHandle shown) { showMaterialOnSphere(shown); });

    // the panel loaded the material before this connection existed, so the sphere is dressed here
    showMaterialOnSphere(handle());

    addPanel(std::make_unique<ViewportPanel>(m_context, ViewportOverlays{.toolbar = true, .orientationGizmo = false}), previewTabBar);
}

void MaterialEditorWorkspace::buildHotbar()
{
    Amethyst::TextButton *compileButton = addHotbarButton("Compile", [this]() {
        if (m_nodeEditor != nullptr) {
            m_nodeEditor->compileGraph();
        }
    });
    ControlTooltip_attach(*compileButton, MATERIAL_EDITOR_TOOLTIPS[MATERIAL_EDITOR_TOOLTIP_COMPILE], m_editor);
}

void MaterialEditorWorkspace::showMaterialOnSphere(Rapture::AssetHandle handle)
{
    m_previewSphere->setMaterial(0, handle);
}

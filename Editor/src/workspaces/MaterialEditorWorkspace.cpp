#include "MaterialEditorWorkspace.h"

#include "Icons.h"
#include "panels/MaterialDetailsPanel.h"
#include "panels/NodeEditorPanel.h"
#include "panels/ViewportPanel.h"
#include "ui/ControlTooltip.h"
#include "ui/controls/IconLabelButton.h"

#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/assets/asset_manager/ReservedAssets.h>
#include <rapture/core/utils/EnginePaths.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Camera3D.h>
#include <rapture/scene/instances/DirectionalLight3D.h>
#include <rapture/scene/instances/Environment.h>
#include <rapture/scene/instances/SceneObject.h>
#include <rapture/scene/instances/StaticMesh3D.h>

#include <components/extensions/ui_list_layout.h>
#include <components/text_label.h>
#include <components/ui_scope.h>

#include <array>
#include <format>
#include <string>

enum MaterialEditorTooltip {
    MATERIAL_EDITOR_TOOLTIP_COMPILE,
    MATERIAL_EDITOR_TOOLTIP_COUNT
};

static constexpr std::array<ControlTooltip, MATERIAL_EDITOR_TOOLTIP_COUNT> MATERIAL_EDITOR_TOOLTIPS = {{
    {.name = "Compile", .description = "Builds the material's shader from its graph"},
}};

static constexpr std::string_view PREVIEW_SCENE_NAME = "MaterialPreview";

static constexpr float SIDE_COLUMN_SPLIT = 0.28f;
static constexpr float PREVIEW_SPLIT = 0.6f;

static constexpr std::string_view COMPILE_BUTTON_CLASS = "hotbar-primary-button";
static constexpr float COMPILE_STATUS_DOT_SIZE = 8.0f;
static constexpr float COMPILE_STATUS_GAP = 8.0f;
static constexpr std::array<std::string_view, MaterialEditorWorkspace::COMPILE_STATUS_COUNT> COMPILE_STATUS_DOT_CLASSES = {
    "compile-status-compiled",
    "compile-status-changed",
    "compile-status-failed",
};

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
    Amethyst::TabBar *previewTabBar = nullptr;
    Amethyst::TabBar *detailsTabBar = nullptr;
    Amethyst::TabBar *graphTabBar = nullptr;
    Amethyst::DockScope(*m_contentDock)
        .split(
            Amethyst::SplitAxis::VERTICAL, SIDE_COLUMN_SPLIT,
            [&](Amethyst::DockScope &side) {
                side.split(
                    Amethyst::SplitAxis::HORIZONTAL, PREVIEW_SPLIT,
                    [&](Amethyst::DockScope &top) { top.panel([&](Amethyst::TabBarScope &tb) { previewTabBar = &tb.component; }); },
                    [&](Amethyst::DockScope &bottom) { bottom.panel([&](Amethyst::TabBarScope &tb) { detailsTabBar = &tb.component; }); });
            },
            [&](Amethyst::DockScope &main) { main.panel([&](Amethyst::TabBarScope &tb) { graphTabBar = &tb.component; }); });

    addPanel(std::make_unique<ViewportPanel>(m_context, ViewportOverlays{.toolbar = true, .orientationGizmo = false}), previewTabBar);

    m_nodeEditor = addPanel(std::make_unique<NodeEditorPanel>(m_context, handle()), graphTabBar);
    m_materialSelectedConn =
        m_nodeEditor->onMaterialSelectionChanged().connect([this](Rapture::AssetHandle shown) { showMaterialOnSphere(shown); });
    m_compiledConn = m_nodeEditor->onCompiled().connect(
        [this](bool succeeded, size_t nodeCount) {
            (void)nodeCount;
            showCompileStatus(succeeded ? COMPILE_STATUS_COMPILED : COMPILE_STATUS_FAILED);
        });
    m_nodeEditedConn = m_nodeEditor->onNodeEdited().connect([this](uint32_t nodeId) {
        (void)nodeId;
        showCompileStatus(COMPILE_STATUS_CHANGED);
    });
    m_nodeValuesConn = m_nodeEditor->onNodeValuesChanged().connect([this](uint32_t nodeId) {
        (void)nodeId;
        showCompileStatus(COMPILE_STATUS_CHANGED);
    });

    addPanel(std::make_unique<MaterialDetailsPanel>(m_context, handle(), *m_nodeEditor), detailsTabBar);

    // the panel loaded the material before these connections existed, so the sphere and status are set here
    showMaterialOnSphere(handle());
    showCompileStatus(COMPILE_STATUS_COMPILED);
}

void MaterialEditorWorkspace::buildHotbar()
{
    IconLabelButton *compileButton = addHotbarButton("Compile", Icons::SVG_BUILD, COMPILE_BUTTON_CLASS, [this]() {
        if (m_nodeEditor != nullptr) {
            m_nodeEditor->compileGraph();
        }
    });
    ControlTooltip_attach(*compileButton, MATERIAL_EDITOR_TOOLTIPS[MATERIAL_EDITOR_TOOLTIP_COMPILE], m_editor);
    addHotbarSeparator();

    addFittedHotbarItem([this](Amethyst::UIScope &slot) {
        auto *row = slot.get().as<Amethyst::Frame>()->addExtension<Amethyst::UIListLayout>();
        row->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
        row->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
        row->innerPadding = Amethyst::UDim::fromOffset(COMPILE_STATUS_GAP);

        slot.frame(
            {
                .base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(COMPILE_STATUS_DOT_SIZE, COMPILE_STATUS_DOT_SIZE)},
            },
            [this](Amethyst::FrameScope &dot) { m_compileStatusDot = &dot.component; });
        slot.textLabel(
            {
                .classes = {"compile-status-note"},
                .base = {.automaticSize = Amethyst::AutomaticSize::X, .layoutOrder = 1, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)},
            },
            [this](Amethyst::TextLabelScope &label) { m_compileStatusLabel = &label.component; });
        m_compileStatusDot->ignore(Amethyst::INTERACTION_CATEGORY_ALL);
        m_compileStatusLabel->ignore(Amethyst::INTERACTION_CATEGORY_ALL);
    });
}

void MaterialEditorWorkspace::showCompileStatus(CompileStatus status)
{
    m_compileStatusDot->setClasses({std::string(COMPILE_STATUS_DOT_CLASSES[status])});
    switch (status) {
    case COMPILE_STATUS_COMPILED:
        m_compileStatusLabel->setText(std::format("Compiled, {} nodes", m_nodeEditor->getNodeCount()));
        break;
    case COMPILE_STATUS_CHANGED:
        m_compileStatusLabel->setText("Changed since the last compile");
        break;
    case COMPILE_STATUS_FAILED:
        m_compileStatusLabel->setText("Compile failed, see the log");
        break;
    case COMPILE_STATUS_COUNT:
        break;
    }
}

void MaterialEditorWorkspace::showMaterialOnSphere(Rapture::AssetHandle handle)
{
    m_previewSphere->setMaterial(0, handle);
}

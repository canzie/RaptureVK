#include "MaterialEditorWorkspace.h"

#include "Icons.h"
#include "panels/NodeEditorPanel.h"
#include "panels/ViewportPanel.h"

#include <assets/asset_manager/AssetManager.h>
#include <assets/materials/AMaterialInstance.h>
#include <core/ecs/entity_accessor.h>
#include <core/utils/EnginePaths.h>
#include <scene/Scene.h>
#include <scene/components/Components.h>
#include <scene/instances/Environment.h>
#include <scene/systems/Transforms.h>

static constexpr std::string_view PREVIEW_SCENE_NAME = "MaterialPreview";

static constexpr float PREVIEW_SPLIT = 0.7f;

static constexpr float SKY_INTENSITY = 1.0f;
static constexpr float SUN_INTENSITY = 3.0f;

// TODO: make this a reserved asset, so opening a material never reaches the disk
static Rapture::AssetRef s_defaultSkybox()
{
    static Rapture::AssetRef skybox =
        Rapture::AssetManager::importAsset(Rapture::EnginePaths::assetDirectory() / "textures/cubemaps/default.cubemap");
    return skybox;
}

MaterialEditorWorkspace::MaterialEditorWorkspace(const EditorContext &context, Rapture::AssetHandle handle)
    : PreviewWorkspace(context, handle, PREVIEW_SCENE_NAME)
{
    Rapture::Scene *scene = preview().scene();

    m_previewSphere = scene->createSphere("Preview Sphere");

    Rapture::ecs::EntityAccessor camera = scene->createEntity("Preview Camera");
    auto &cameraTransform = camera.add<Rapture::TransformComponent>();
    cameraTransform.local = Rapture::transform::compose(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(1.0f));
    cameraTransform.world = cameraTransform.local;
    camera.add<Rapture::CameraComponent>(60.0f, 16.0f / 9.0f, 0.1f, 100.0f);

    Rapture::ecs::EntityAccessor light = scene->createEntity("Preview Light");
    auto &lightTransform = light.add<Rapture::TransformComponent>();
    lightTransform.local = Rapture::transform::compose(glm::vec3(0.0f), glm::vec3(-0.6f, 0.5f, 0.0f), glm::vec3(1.0f));
    lightTransform.world = lightTransform.local;
    light.add<Rapture::DirectionalLightComponent>(glm::vec3(1.0f), SUN_INTENSITY);

    Rapture::Environment *environment = scene->environment();
    Rapture::AssetRef skybox = s_defaultSkybox();
    if (environment != nullptr && skybox) {
        environment->setSkybox(skybox->handle());
        environment->setSkyIntensity(SKY_INTENSITY);
    }

    m_context.viewport = preview().createViewport(camera);

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

    addPanel(std::make_unique<ViewportPanel>(m_context), previewTabBar);
}

void MaterialEditorWorkspace::buildHotbar()
{
    addHotbarButton("Compile", [this]() {
        if (m_nodeEditor != nullptr) {
            m_nodeEditor->compileGraph();
        }
    });
}

void MaterialEditorWorkspace::showMaterialOnSphere(Rapture::AssetHandle handle)
{
    if (!m_previewSphere.isValid()) {
        return;
    }

    Rapture::Ref<Rapture::AMaterialInstance> ref = Rapture::AssetManager::getAsset<Rapture::AMaterialInstance>(handle);
    if (!ref) {
        return;
    }

    if (!m_previewSphere.has<Rapture::MaterialComponent>()) {
        m_previewSphere.add<Rapture::MaterialComponent>(std::move(ref));
        return;
    }
    m_previewSphere.write<Rapture::MaterialComponent>()->materials.assign(1, std::move(ref));
}

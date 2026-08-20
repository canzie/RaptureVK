#include "SkeletalMeshWorkspace.h"

#include "layers/panels/ViewportPanel.h"
#include "layers/panels/components/asset_visuals.h"

#include <assets/asset_manager/AssetManager.h>
#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>
#include <scene/Scene.h>
#include <scene/instances/SceneObject.h>
#include <scene/instances/SkeletalMesh3D.h>

static constexpr std::string_view PREVIEW_SCENE_NAME = "SkeletalMeshPreview";

static constexpr float HOTBAR_BUTTON_WIDTH = 150.0f;
static constexpr float HOTBAR_BUTTON_HEIGHT = 28.0f;

SkeletalMeshWorkspace::SkeletalMeshWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services,
                                             Rapture::AssetHandle handle)
    : AssetPreviewWorkspace(staticKind(), services, handle)
{
    setupBase(tabBar, assetName(), Asset_iconForType(Rapture::ASSET_SKELETAL_MESH));

    setupDockingLayer();
    setupPreviewScene(PREVIEW_SCENE_NAME);

    auto *mesh = previewScene()->root()->add<Rapture::SkeletalMesh3D>(assetName());
    mesh->setMesh(handle);

    // authored geometry can sit anywhere, and the scene around it is built at the origin
    const glm::vec3 min = mesh->boundsMin();
    const glm::vec3 max = mesh->boundsMax();
    const glm::vec3 offset = -(min + max) * 0.5f;
    mesh->setPosition(offset);
    setFocusBounds(min + offset, max + offset);

    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::DockScope(*m_dockingLayer).panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; });

    if (viewportTabBar != nullptr) {
        m_panels.push_back(std::make_unique<ViewportPanel>(viewportTabBar, m_context));
    }

    setupHotbar();
    applyStoredLayout();
}

void SkeletalMeshWorkspace::setupHotbar()
{
    if (m_hotbar == nullptr) {
        return;
    }

    auto *layout = m_hotbar->addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    layout->innerPadding = Amethyst::UDim::fromOffset(6.0f);

    Amethyst::UIScope(*m_hotbar).textButton(
        {.classes = {"generic-text-button"},
         .base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(HOTBAR_BUTTON_WIDTH, HOTBAR_BUTTON_HEIGHT)},
         .text = {.fontSize = 14.0f,
                  .textXAlignment = Amethyst::TextXAlignment::CENTER,
                  .textYAlignment = Amethyst::TextYAlignment::CENTER},
         .label = "Convert Pose to Static"},
        [](Amethyst::TextButtonScope &b) {
            // TODO: bake the current pose into a static mesh asset
            b.component.onMouseButton1ClickCb = []() { return Amethyst::EventResult::CONSUMED; };
        });
}

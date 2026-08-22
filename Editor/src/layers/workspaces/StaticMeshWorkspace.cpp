#include "StaticMeshWorkspace.h"

#include "layers/panels/AssetDetailsPanel.h"
#include "layers/panels/ViewportPanel.h"
#include "layers/panels/components/asset_visuals.h"

#include <assets/asset_manager/AssetManager.h>
#include <scene/Scene.h>
#include <scene/instances/SceneObject.h>
#include <scene/instances/StaticMesh3D.h>

static constexpr std::string_view PREVIEW_SCENE_NAME = "StaticMeshPreview";

static constexpr float DETAILS_SPLIT = 0.75f;

StaticMeshWorkspace::StaticMeshWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services,
                                         Rapture::AssetHandle handle)
    : AssetPreviewWorkspace(staticKind(), services, handle)
{
    setupBase(tabBar, assetName(), Asset_iconForType(Rapture::ASSET_STATIC_MESH));

    setupDockingLayer();
    setupPreviewScene(PREVIEW_SCENE_NAME);

    auto *mesh = previewScene()->root()->add<Rapture::StaticMesh3D>(assetName());
    mesh->setMesh(handle);

    // authored geometry can sit anywhere, and the scene around it is built at the origin
    const glm::vec3 min = mesh->boundsMin();
    const glm::vec3 max = mesh->boundsMax();
    const glm::vec3 offset = -(min + max) * 0.5f;
    mesh->setPosition(offset);
    setFocusBounds(min + offset, max + offset);

    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::TabBar *detailsTabBar = nullptr;
    Amethyst::DockScope(*m_dockingLayer)
        .split(
            Amethyst::SplitAxis::VERTICAL, DETAILS_SPLIT,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) { r.panel([&](Amethyst::TabBarScope &tb) { detailsTabBar = &tb.component; }); });

    if (viewportTabBar != nullptr) {
        m_panels.push_back(std::make_unique<ViewportPanel>(viewportTabBar, m_context));
    }
    if (detailsTabBar != nullptr) {
        m_panels.push_back(std::make_unique<AssetDetailsPanel>(detailsTabBar, m_context, handle));
    }

    applyStoredLayout();
}

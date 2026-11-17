#include "StaticMeshWorkspace.h"

#include "panels/AssetDetailsPanel.h"
#include "panels/ViewportPanel.h"
#include "ui/AssetVisuals.h"

#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/SceneObject.h>
#include <rapture/scene/instances/StaticMesh3D.h>

static constexpr std::string_view PREVIEW_SCENE_NAME = "StaticMeshPreview";

static constexpr float DETAILS_SPLIT = 0.75f;

StaticMeshWorkspace::StaticMeshWorkspace(const EditorContext &context, Rapture::AssetHandle handle)
    : PreviewWorkspace(context, handle, PREVIEW_SCENE_NAME)
{
    preview().addGroundAndSky();

    auto *mesh = preview().scene()->root()->add<Rapture::StaticMesh3D>(assetName());
    mesh->setMesh(handle);

    m_context.viewport = preview().createViewport();
    m_context.editorCamera = preview().editorCamera();

    Rapture::Mesh3D *placed[] = {mesh};
    preview().centreMeshOnOrigin(placed);
}

std::string_view StaticMeshWorkspace::tabIcon() const
{
    return Asset_iconForType(Rapture::ASSET_STATIC_MESH);
}

void StaticMeshWorkspace::build()
{
    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::TabBar *detailsTabBar = nullptr;
    Amethyst::DockScope(*m_contentDock)
        .split(
            Amethyst::SplitAxis::VERTICAL, DETAILS_SPLIT,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) { r.panel([&](Amethyst::TabBarScope &tb) { detailsTabBar = &tb.component; }); });

    addPanel(std::make_unique<ViewportPanel>(m_context), viewportTabBar);
    addPanel(std::make_unique<AssetDetailsPanel>(m_context, handle()), detailsTabBar);
}

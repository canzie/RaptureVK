#include "StaticMeshWorkspace.h"

#include "Icons.h"
#include "panels/AssetDetailsPanel.h"
#include "panels/ViewportPanel.h"
#include "ui/AssetVisuals.h"

#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/StaticMesh3D.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

static constexpr std::string_view PREVIEW_SCENE_NAME = "StaticMeshPreview";

static constexpr float VIEWPORT_SPLIT = 0.68f;

static const glm::vec3 Z_UP_TO_Y_UP = glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f);

StaticMeshWorkspace::StaticMeshWorkspace(const EditorContext &context, Rapture::AssetHandle handle)
    : PreviewWorkspace(context, handle, PREVIEW_SCENE_NAME)
{
    preview().addSky();

    m_mesh = preview().scene()->root()->add<Rapture::StaticMesh3D>(assetName());
    m_mesh->setMesh(handle);

    m_context.viewport = preview().createViewport();
    m_context.editorCamera = preview().editorCamera();

    Rapture::Mesh3D *placed[] = {m_mesh};
    m_centringOffset = preview().centreMeshOnOrigin(placed);
    m_centredBoundsMin = m_mesh->boundsMin() + m_centringOffset;
    m_centredBoundsMax = m_mesh->boundsMax() + m_centringOffset;

    m_bakeTransform.onChanged = [this]() { previewBakeTransform(); };
}

StaticMeshWorkspace::~StaticMeshWorkspace()
{
    destroyPanels();
}

void StaticMeshWorkspace::previewBakeTransform()
{
    const glm::vec3 rotation = m_bakeTransform.upAxis == MESH_UP_AXIS_Z ? Z_UP_TO_Y_UP : glm::vec3(0.0f);

    m_mesh->setScale(glm::vec3(m_bakeTransform.scale));
    m_mesh->setRotation(rotation);
    m_mesh->setPosition(glm::quat(rotation) * (m_centringOffset * m_bakeTransform.scale));

    preview().fitViewRangeTo(m_centredBoundsMin * m_bakeTransform.scale, m_centredBoundsMax * m_bakeTransform.scale);
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
            Amethyst::SplitAxis::VERTICAL, VIEWPORT_SPLIT,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) { r.panel([&](Amethyst::TabBarScope &tb) { detailsTabBar = &tb.component; }); });

    addPanel(std::make_unique<ViewportPanel>(m_context, ViewportOverlays{.toolbar = true, .orientationGizmo = true}), viewportTabBar);
    addPanel(std::make_unique<AssetDetailsPanel>(m_context, handle(), m_bakeTransform), detailsTabBar);
}

void StaticMeshWorkspace::buildHotbar()
{
    addHotbarButton("Save", Icons::SVG_SAVE, [this]() { save(); });
    addHotbarSeparator();
    addHotbarButton("Reimport", Icons::SVG_REFRESH, [this]() { reimport(); });
    addHotbarSeparator();
}

void StaticMeshWorkspace::save()
{
    // TODO: bake m_bakeTransform into the mesh's geometry before it is written
    saveOpenAsset();
}

void StaticMeshWorkspace::reimport()
{
    // TODO: reimport from the source file
}

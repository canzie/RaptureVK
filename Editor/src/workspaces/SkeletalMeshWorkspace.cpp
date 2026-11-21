#include "SkeletalMeshWorkspace.h"

#include "Icons.h"
#include "panels/AssetDetailsPanel.h"
#include "panels/SkeletonHierarchyPanel.h"
#include "panels/ViewportPanel.h"
#include "ui/AssetVisuals.h"

#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/assets/meshes/ASkeletalMesh.h>
#include <rapture/core/utils/Log.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/SceneObject.h>
#include <rapture/scene/instances/SkeletalMesh3D.h>
#include <rapture/scene/instances/SkeletonPose.h>

static constexpr std::string_view PREVIEW_SCENE_NAME = "SkeletalMeshPreview";

static constexpr float HIERARCHY_SPLIT = 0.2f;

static constexpr float DETAILS_SPLIT = 0.66f;

SkeletalMeshWorkspace::SkeletalMeshWorkspace(const EditorContext &context, Rapture::AssetHandle handle)
    : PreviewWorkspace(context, handle, PREVIEW_SCENE_NAME)
{
    preview().addGroundAndSky();

    m_mesh = preview().scene()->root()->add<Rapture::SkeletalMesh3D>(assetName());
    m_mesh->setMesh(handle);

    m_context.viewport = preview().createViewport();
    m_context.editorCamera = preview().editorCamera();

    setupPose();

    Rapture::Mesh3D *placed[] = {m_mesh};
    const glm::vec3 centringOffset = preview().centreMeshOnOrigin(placed);
    if (m_pose != nullptr) {
        m_pose->setPosition(centringOffset);
    }
}

std::string_view SkeletalMeshWorkspace::tabIcon() const
{
    return Asset_iconForType(Rapture::ASSET_SKELETAL_MESH);
}

void SkeletalMeshWorkspace::setupPose()
{
    Rapture::Ref<Rapture::ASkeletalMesh> mesh = Rapture::AssetManager::getAsset<Rapture::ASkeletalMesh>(handle());
    if (!mesh) {
        RP_ERROR("skeletal mesh {} could not be resolved, showing it without its skeleton", handle());
        return;
    }

    auto owned = std::make_unique<Rapture::SkeletonPose>(*preview().scene(), "Pose", mesh->skeleton());
    m_pose = owned.get();
    preview().scene()->root()->addChild(std::move(owned));
    m_pose->ready();

    m_mesh->setPose(m_pose);
}

void SkeletalMeshWorkspace::build()
{
    Amethyst::TabBar *hierarchyTabBar = nullptr;
    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::TabBar *detailsTabBar = nullptr;
    Amethyst::DockScope(*m_contentDock)
        .split(
            Amethyst::SplitAxis::VERTICAL, HIERARCHY_SPLIT,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { hierarchyTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) {
                r.split(
                    Amethyst::SplitAxis::VERTICAL, DETAILS_SPLIT,
                    [&](Amethyst::DockScope &v) { v.panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; }); },
                    [&](Amethyst::DockScope &d) { d.panel([&](Amethyst::TabBarScope &tb) { detailsTabBar = &tb.component; }); });
            });

    if (m_pose != nullptr) {
        addPanel(std::make_unique<SkeletonHierarchyPanel>(m_context, m_pose), hierarchyTabBar);
    }
    addPanel(std::make_unique<ViewportPanel>(m_context, ViewportOverlays{.toolbar = true, .orientationGizmo = true}), viewportTabBar);
    addPanel(std::make_unique<AssetDetailsPanel>(m_context, handle()), detailsTabBar);
}

void SkeletalMeshWorkspace::buildHotbar()
{
    addHotbarButton("Save", Icons::SVG_SAVE, [this]() { saveOpenAsset(); });
    addHotbarSeparator();
    addHotbarButton("Reimport", Icons::SVG_REFRESH, [this]() { reimport(); });
    addHotbarSeparator();
    // TODO: bake the current pose into a static mesh asset
    addHotbarButton("Convert Pose to Static", []() {});
    addHotbarSeparator();
}

void SkeletalMeshWorkspace::reimport()
{
    // TODO: reimport from the source file
}

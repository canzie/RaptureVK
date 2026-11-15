#include "SkeletonWorkspace.h"

#include "panels/AssetDetailsPanel.h"
#include "panels/SkeletonHierarchyPanel.h"
#include "panels/ViewportPanel.h"
#include "ui/AssetVisuals.h"
#include "ui/controls/ModeDropdown.h"

#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/assets/skeletons/ASkeleton.h>
#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/rp_assert.h>
#include <rapture/renderer/viewport/Viewport.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/SceneObject.h>
#include <rapture/scene/instances/SkeletalMesh3D.h>
#include <rapture/scene/instances/SkeletonPose.h>

static constexpr std::string_view PREVIEW_SCENE_NAME = "SkeletonPreview";

static constexpr float DETAILS_SPLIT = 0.75f;

static constexpr float HIERARCHY_SPLIT = 0.45f;

static constexpr float MODE_DROPDOWN_WIDTH = 120.0f;

static constexpr EditorMode OFFERED_MODES[] = {EDITOR_MODE_OBJECT, EDITOR_MODE_EDIT, EDITOR_MODE_POSE};

SkeletonWorkspace::SkeletonWorkspace(const EditorContext &context, Rapture::AssetHandle handle)
    : PreviewWorkspace(context, handle, PREVIEW_SCENE_NAME)
{
    preview().addGroundAndSky();
    m_context.viewport = preview().createViewport();

    setupPose();
    spawnPreviewMeshes();
}

std::string_view SkeletonWorkspace::tabIcon() const
{
    return Asset_iconForType(Rapture::ASSET_SKELETON);
}

void SkeletonWorkspace::build()
{
    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::TabBar *hierarchyTabBar = nullptr;
    Amethyst::TabBar *detailsTabBar = nullptr;
    Amethyst::DockScope(*m_contentDock)
        .split(
            Amethyst::SplitAxis::VERTICAL, DETAILS_SPLIT,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) {
                r.split(
                    Amethyst::SplitAxis::HORIZONTAL, HIERARCHY_SPLIT,
                    [&](Amethyst::DockScope &t) { t.panel([&](Amethyst::TabBarScope &tb) { hierarchyTabBar = &tb.component; }); },
                    [&](Amethyst::DockScope &b) { b.panel([&](Amethyst::TabBarScope &tb) { detailsTabBar = &tb.component; }); });
            });

    auto viewport = std::make_unique<ViewportPanel>(m_context);
    m_viewportPanel = viewport.get();
    addPanel(std::move(viewport), viewportTabBar);

    if (m_pose != nullptr) {
        addPanel(std::make_unique<SkeletonHierarchyPanel>(m_context, m_pose), hierarchyTabBar);
    }
    addPanel(std::make_unique<AssetDetailsPanel>(m_context, handle()), detailsTabBar);
}

void SkeletonWorkspace::buildHotbar()
{
    addHotbarItem(MODE_DROPDOWN_WIDTH, [this](Amethyst::UIScope &slot) {
        ModeDropdown_add(slot.get(), OFFERED_MODES, [this](EditorMode mode) { setMode(mode); });
    });
}

void SkeletonWorkspace::onUpdate(float dt)
{
    PreviewWorkspace::onUpdate(dt);

    if (!isActive()) {
        return;
    }

    // the list changes from inside a picker's callback, which is no place to tear down scene objects
    if (m_previewMeshesPending) {
        m_previewMeshesPending = false;
        spawnPreviewMeshes();
    }

    if (m_pose == nullptr || m_viewportPanel == nullptr) {
        return;
    }

    Rapture::Viewport *viewport = m_viewportPanel->getViewport();
    RP_ASSERT(viewport != nullptr, "the skeleton workspace has no viewport to draw its skeleton into");

    m_skeletonGizmo.addSkeleton(*m_pose);
    m_skeletonGizmo.submit(viewport->getGizmoDrawList());
}

void SkeletonWorkspace::setupPose()
{
    Rapture::Ref<Rapture::ASkeleton> skeleton = Rapture::AssetManager::getAsset<Rapture::ASkeleton>(handle());
    if (!skeleton) {
        RP_ERROR("skeleton {} could not be resolved, showing nothing", handle());
        return;
    }

    auto owned = std::make_unique<Rapture::SkeletonPose>(*preview().scene(), "Pose", handle());
    m_pose = owned.get();
    preview().scene()->root()->addChild(std::move(owned));
    m_pose->ready();

    m_modeChangedConn = onModeChanged.connect([this](EditorMode mode) { m_skeletonGizmo.setMode(mode); });

    m_previewMeshesChangedConn = skeleton->onPreviewMeshesChanged.connect([this]() { m_previewMeshesPending = true; });
}

void SkeletonWorkspace::spawnPreviewMeshes()
{
    Rapture::Ref<Rapture::ASkeleton> skeleton = Rapture::AssetManager::getAsset<Rapture::ASkeleton>(handle());
    if (!skeleton || m_pose == nullptr) {
        return;
    }

    for (Rapture::SkeletalMesh3D *object : m_previewObjects) {
        preview().scene()->destroyInstance(object);
    }
    m_previewObjects.clear();

    Rapture::SceneObject *root = preview().scene()->root();

    std::vector<Rapture::Mesh3D *> placed;
    for (Rapture::AssetHandle mesh : skeleton->previewMeshes()) {
        auto *object = root->add<Rapture::SkeletalMesh3D>(Rapture::AssetManager::getAssetMetadata(mesh).getName());
        object->setMesh(mesh);
        object->setPose(m_pose);
        m_previewObjects.push_back(object);
        placed.push_back(object);
    }

    m_pose->setPosition(preview().centreMeshOnOrigin(placed));
}

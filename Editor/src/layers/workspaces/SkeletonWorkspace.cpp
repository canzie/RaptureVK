#include "SkeletonWorkspace.h"

#include "layers/EditorLayout.h"
#include "layers/panels/AssetDetailsPanel.h"
#include "layers/panels/SkeletonHierarchyPanel.h"
#include "layers/panels/ViewportPanel.h"
#include "layers/panels/components/asset_visuals.h"
#include "layers/panels/components/mode_dropdown.h"

#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>

#include <assets/asset_manager/AssetManager.h>
#include <assets/skeletons/ASkeleton.h>
#include <core/utils/Log.h>
#include <core/utils/rp_assert.h>
#include <renderer/viewport/Viewport.h>
#include <scene/Scene.h>
#include <scene/instances/SceneObject.h>
#include <scene/instances/SkeletalMesh3D.h>
#include <scene/instances/SkeletonPose.h>

#include <vector>

static constexpr std::string_view PREVIEW_SCENE_NAME = "SkeletonPreview";

static constexpr float DETAILS_SPLIT = 0.75f;

static constexpr float HIERARCHY_SPLIT = 0.45f;

static constexpr EditorMode OFFERED_MODES[] = {EDITOR_MODE_OBJECT, EDITOR_MODE_EDIT, EDITOR_MODE_POSE};

SkeletonWorkspace::SkeletonWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services, Rapture::AssetHandle handle)
    : AssetPreviewWorkspace(staticKind(), services, handle)
{
    setupBase(tabBar, assetName(), Asset_iconForType(Rapture::ASSET_SKELETON));

    setupDockingLayer();
    setupPreviewScene(PREVIEW_SCENE_NAME);
    setupPose();
    spawnPreviewMeshes();

    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::TabBar *hierarchyTabBar = nullptr;
    Amethyst::TabBar *detailsTabBar = nullptr;
    Amethyst::DockScope(*m_dockingLayer)
        .split(
            Amethyst::SplitAxis::VERTICAL, DETAILS_SPLIT,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) {
                r.split(
                    Amethyst::SplitAxis::HORIZONTAL, HIERARCHY_SPLIT,
                    [&](Amethyst::DockScope &t) { t.panel([&](Amethyst::TabBarScope &tb) { hierarchyTabBar = &tb.component; }); },
                    [&](Amethyst::DockScope &b) { b.panel([&](Amethyst::TabBarScope &tb) { detailsTabBar = &tb.component; }); });
            });

    if (viewportTabBar != nullptr) {
        auto panel = std::make_unique<ViewportPanel>(viewportTabBar, m_context);
        m_viewportPanel = panel.get();
        m_panels.push_back(std::move(panel));
    }
    if (hierarchyTabBar != nullptr && m_pose != nullptr) {
        m_panels.push_back(std::make_unique<SkeletonHierarchyPanel>(hierarchyTabBar, m_context, m_pose));
    }
    if (detailsTabBar != nullptr) {
        m_panels.push_back(std::make_unique<AssetDetailsPanel>(detailsTabBar, m_context, handle));
    }

    setupHotbar();
    applyStoredLayout();
}

void SkeletonWorkspace::onUpdate(float dt)
{
    AssetPreviewWorkspace::onUpdate(dt);

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

void SkeletonWorkspace::setupHotbar()
{
    if (m_hotbar == nullptr) {
        return;
    }

    auto *layout = m_hotbar->addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    layout->innerPadding = Amethyst::UDim::fromOffset(EDITOR_HOTBAR_PADDING);

    ModeDropdown_add(*m_hotbar, OFFERED_MODES, [this](EditorMode mode) { setMode(mode); });
}

void SkeletonWorkspace::setupPose()
{
    Rapture::AssetPtr<Rapture::ASkeleton> skeleton(Rapture::AssetManager::getAsset(handle()));
    if (!skeleton) {
        RP_ERROR("skeleton {} could not be resolved, showing nothing", handle());
        return;
    }

    auto owned = std::make_unique<Rapture::SkeletonPose>(*previewScene(), "Pose", handle());
    m_pose = owned.get();
    previewScene()->root()->addChild(std::move(owned));
    m_pose->ready();

    m_modeChangedConn = onModeChanged.connect([this](EditorMode mode) { m_skeletonGizmo.setMode(mode); });

    m_previewMeshesChangedConn = skeleton->onPreviewMeshesChanged.connect([this]() { m_previewMeshesPending = true; });
}

void SkeletonWorkspace::spawnPreviewMeshes()
{
    Rapture::AssetPtr<Rapture::ASkeleton> skeleton(Rapture::AssetManager::getAsset(handle()));
    if (!skeleton || m_pose == nullptr) {
        return;
    }

    for (Rapture::SkeletalMesh3D *object : m_previewObjects) {
        previewScene()->destroyInstance(object);
    }
    m_previewObjects.clear();

    Rapture::SceneObject *root = previewScene()->root();

    glm::vec3 min(0.0f);
    glm::vec3 max(0.0f);

    for (Rapture::AssetHandle mesh : skeleton->previewMeshes()) {
        auto *object = root->add<Rapture::SkeletalMesh3D>(Rapture::AssetManager::getAssetMetadata(mesh).getName());
        object->setMesh(mesh);
        object->setPose(m_pose);
        m_previewObjects.push_back(object);

        min = glm::min(min, object->boundsMin());
        max = glm::max(max, object->boundsMax());
    }

    // authored geometry can sit anywhere, and the scene around it is built at the origin, so every
    // mesh shifts by the same amount to keep them together
    const glm::vec3 offset = -(min + max) * 0.5f;
    for (Rapture::SkeletalMesh3D *object : m_previewObjects) {
        object->setPosition(offset);
    }
    m_pose->setPosition(offset);

    setFocusBounds(min + offset, max + offset);
}

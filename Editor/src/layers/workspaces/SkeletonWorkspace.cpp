#include "SkeletonWorkspace.h"

#include "layers/panels/ViewportPanel.h"
#include "layers/panels/components/asset_visuals.h"

#include <assets/asset_manager/AssetManager.h>
#include <assets/skeletons/ASkeleton.h>
#include <core/utils/Log.h>
#include <scene/Scene.h>
#include <scene/instances/SceneObject.h>
#include <scene/instances/SkeletalMesh3D.h>
#include <scene/instances/SkeletonPose.h>

#include <vector>

static constexpr std::string_view PREVIEW_SCENE_NAME = "SkeletonPreview";

SkeletonWorkspace::SkeletonWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services, Rapture::AssetHandle handle)
    : AssetPreviewWorkspace(staticKind(), services, handle)
{
    setupBase(tabBar, assetName(), Asset_iconForType(Rapture::ASSET_SKELETON));

    setupDockingLayer();
    setupPreviewScene(PREVIEW_SCENE_NAME);
    spawnPreviewMeshes();

    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::DockScope(*m_dockingLayer).panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; });

    if (viewportTabBar != nullptr) {
        m_panels.push_back(std::make_unique<ViewportPanel>(viewportTabBar, m_context));
    }

    applyStoredLayout();
}

void SkeletonWorkspace::spawnPreviewMeshes()
{
    Rapture::AssetPtr<Rapture::ASkeleton> skeleton(Rapture::AssetManager::getAsset(handle()));
    if (!skeleton) {
        RP_ERROR("skeleton {} could not be resolved, showing nothing", handle());
        return;
    }

    Rapture::SceneObject *root = previewScene()->root();

    auto owned = std::make_unique<Rapture::SkeletonPose>(*previewScene(), "Pose", handle());
    Rapture::SkeletonPose *pose = owned.get();
    root->addChild(std::move(owned));
    pose->ready();

    std::vector<Rapture::SkeletalMesh3D *> objects;
    glm::vec3 min(0.0f);
    glm::vec3 max(0.0f);

    for (Rapture::AssetHandle mesh : skeleton->previewMeshes()) {
        auto *object = root->add<Rapture::SkeletalMesh3D>(Rapture::AssetManager::getAssetMetadata(mesh).getName());
        object->setMesh(mesh);
        object->setPose(pose);
        objects.push_back(object);

        min = glm::min(min, object->boundsMin());
        max = glm::max(max, object->boundsMax());
    }

    // authored geometry can sit anywhere, and the scene around it is built at the origin, so every
    // mesh shifts by the same amount to keep them together
    const glm::vec3 offset = -(min + max) * 0.5f;
    for (Rapture::SkeletalMesh3D *object : objects) {
        object->setPosition(offset);
    }

    setFocusBounds(min + offset, max + offset);
}

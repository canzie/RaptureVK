#include "SkeletonPose.h"

#include "assets/asset_manager/AssetManager.h"
#include "assets/skeletons/Skeleton.h"
#include "core/utils/Log.h"
#include "scene/Scene.h"
#include "scene/components/Components.h"
#include "scene/render_data/SceneRenderData.h"

namespace Rapture {

static constexpr std::string_view KEY_POSE = "pose";
static constexpr std::string_view KEY_SKELETON = "skeleton";

SkeletonPose::SkeletonPose(Scene &scene, std::string_view name) : SceneObject(scene, name)
{
    m_entity.set<SkeletonPoseComponent>();
}

SkeletonPose::SkeletonPose(Scene &scene, std::string_view name, AssetHandle skeleton) : SkeletonPose(scene, name)
{
    bindSkeleton(skeleton);
}

SkeletonPose::~SkeletonPose() = default;

const TypeInfo &SkeletonPose::staticType()
{
    static const TypeInfo type("SkeletonPose", &SceneObject::staticType());
    return type;
}

const TypeInfo &SkeletonPose::type() const
{
    return staticType();
}

void SkeletonPose::bindSkeleton(AssetHandle skeleton)
{
    if (m_skeleton == skeleton && m_skeletonInstance != nullptr) {
        return;
    }

    AssetRef ref = AssetManager::getAsset(skeleton);
    if (!ref) {
        RP_CORE_ERROR("skeleton {} could not be resolved for '{}'", skeleton, name());
        return;
    }

    AssetPtr<Skeleton> resolved(std::move(ref));
    if (!resolved || resolved->getJointCount() == 0) {
        RP_CORE_ERROR("skeleton {} has no joints for '{}' to pose", skeleton, name());
        return;
    }

    SkeletonInstanceManager &manager = scene()->getRenderData()->getSkeletonInstanceManager();

    // the bones of the skeleton being left go back to the arena before the new one asks for its own
    m_skeletonInstance.reset();
    m_skeletonInstance = std::make_unique<SkeletonInstance>(manager.createSkeletonInstance(resolved));
    m_skeleton = skeleton;

    auto component = m_entity.write<SkeletonPoseComponent>();

    component->skeleton = std::move(resolved);
    component->instance = m_skeletonInstance.get();
}

void SkeletonPose::serialize(WriteNode node) const
{
    SceneObject::serialize(node);

    WriteNode pose = node.addObject(KEY_POSE);
    pose.set(KEY_SKELETON, m_skeleton);
}

void SkeletonPose::deserialize(ReadNode node)
{
    SceneObject::deserialize(node);

    ReadNode pose = node.child(KEY_POSE);
    if (!pose.valid()) {
        return;
    }

    AssetHandle skeleton = pose.child(KEY_SKELETON).asU64(INVALID_ASSET_HANDLE);
    if (skeleton != INVALID_ASSET_HANDLE) {
        bindSkeleton(skeleton);
    }
}

} // namespace Rapture

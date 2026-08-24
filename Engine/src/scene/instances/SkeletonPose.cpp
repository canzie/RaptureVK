#include "SkeletonPose.h"

#include "assets/asset_manager/AssetManager.h"
#include "assets/skeletons/ASkeleton.h"
#include "scene/instances/Bone3D.h"
#include "core/utils/Log.h"
#include "scene/Scene.h"
#include "scene/components/Components.h"
#include "scene/render_data/SceneRenderData.h"
#include "scene/systems/Transforms.h"

namespace Rapture {

static constexpr std::string_view KEY_POSE = "pose";
static constexpr std::string_view KEY_SKELETON = "skeleton";

SkeletonPose::SkeletonPose(Scene &scene, std::string_view name) : Node3D(scene, name)
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
    static const TypeInfo type("SkeletonPose", &Node3D::staticType());
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

    AssetPtr<ASkeleton> resolved(std::move(ref));
    if (!resolved || resolved->skeleton().getJointCount() == 0) {
        RP_CORE_ERROR("skeleton {} has no joints for '{}' to pose", skeleton, name());
        return;
    }

    SkeletonInstanceManager &manager = scene()->getRenderData()->getSkeletonInstanceManager();

    // the bones of the skeleton being left go back to the arena before the new one asks for its own
    m_skeletonInstance.reset();
    m_skeletonInstance = std::make_unique<SkeletonInstance>(manager.createSkeletonInstance(resolved));
    m_skeleton = skeleton;

    m_localPose = resolved->skeleton().getRestPose().joints;
    m_boneMatrices.assign(m_localPose.size(), glm::mat4(1.0f));

    {
        auto component = m_entity.write<SkeletonPoseComponent>();

        component->skeleton = std::move(resolved);
        component->instance = m_skeletonInstance.get();
    }

    buildBones();
}

std::span<const Skeleton::JointIndex> SkeletonPose::getJointParents() const
{
    return m_entity.read<SkeletonPoseComponent>().skeleton->skeleton().getParents();
}

void SkeletonPose::buildBones()
{
    m_bones.clear();

    const Skeleton &skeleton = m_entity.read<SkeletonPoseComponent>().skeleton->skeleton();
    const std::vector<Skeleton::JointIndex> &parents = skeleton.getParents();
    const std::vector<std::string> &names = skeleton.getNames();

    m_bones.reserve(names.size());

    // a joint always follows its parent, so the bone it hangs from is built by the time it is reached
    for (Skeleton::JointIndex joint = 0; joint < names.size(); ++joint) {
        const Skeleton::JointIndex parent = parents[joint];

        SceneObject *under = this;
        if (parent != Skeleton::INVALID_JOINT_INDEX) {
            under = m_bones[parent];
        }

        Bone3D *bone = under->add<Bone3D>(names[joint], InternalMode::ENABLED);
        bone->setSerialized(false);
        bone->bindJoint(this, joint);
        m_bones.push_back(bone);
    }

    pushPoseToBones();
}

void SkeletonPose::pushPoseToBones()
{
    for (Skeleton::JointIndex joint = 0; joint < m_bones.size(); ++joint) {
        const Skeleton::JointTransform &local = m_localPose[joint];
        m_bones[joint]->setPosedLocalTransform(transform::compose(local.position, local.rotation, local.scale));
    }

    writeBoneMatrices();
}

void SkeletonPose::setJointLocal(Skeleton::JointIndex joint, const Skeleton::JointTransform &transform)
{
    m_localPose[joint] = transform;
    m_bones[joint]->setPosedLocalTransform(transform::compose(transform.position, transform.rotation, transform.scale));
    writeBoneMatrices();
}

void SkeletonPose::writeBoneMatrices()
{
    if (m_skeletonInstance == nullptr) {
        return;
    }

    const std::span<const Skeleton::JointIndex> parents = getJointParents();

    // a joint always follows its parent, so the chain above it is already accumulated
    for (Skeleton::JointIndex joint = 0; joint < m_boneMatrices.size(); ++joint) {
        const Skeleton::JointTransform &local = m_localPose[joint];
        const glm::mat4 matrix = transform::compose(local.position, local.rotation, local.scale);
        const Skeleton::JointIndex parent = parents[joint];

        m_boneMatrices[joint] = parent == Skeleton::INVALID_JOINT_INDEX ? matrix : m_boneMatrices[parent] * matrix;
    }

    m_skeletonInstance->write(m_boneMatrices);
}

void SkeletonPose::resetToRestPose()
{
    if (m_localPose.empty()) {
        return;
    }

    m_localPose = m_entity.read<SkeletonPoseComponent>().skeleton->skeleton().getRestPose().joints;
    pushPoseToBones();
}

void SkeletonPose::serialize(WriteNode node) const
{
    Node3D::serialize(node);

    WriteNode pose = node.addObject(KEY_POSE);
    pose.set(KEY_SKELETON, m_skeleton);
}

void SkeletonPose::deserialize(ReadNode node)
{
    Node3D::deserialize(node);

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

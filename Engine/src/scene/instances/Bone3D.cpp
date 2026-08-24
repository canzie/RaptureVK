#include "Bone3D.h"

#include "core/utils/rp_assert.h"
#include "scene/instances/SkeletonPose.h"
#include "scene/systems/Transforms.h"

namespace Rapture {

Bone3D::Bone3D(Scene &scene, std::string_view name) : Node3D(scene, name)
{
}

const TypeInfo &Bone3D::staticType()
{
    static const TypeInfo type("Bone3D", &Node3D::staticType());
    return type;
}

const TypeInfo &Bone3D::type() const
{
    return staticType();
}

void Bone3D::bindJoint(SkeletonPose *pose, Skeleton::JointIndex joint)
{
    m_pose = pose;
    m_joint = joint;
}

void Bone3D::setPosedLocalTransform(const glm::mat4 &transform)
{
    setLocalTransformUnreported(transform);
}

void Bone3D::onLocalTransformChanged()
{
    RP_ASSERT(m_pose != nullptr, "'{}' was moved before it was bound to a joint", name());

    Skeleton::JointTransform local;
    transform::decompose(localTransform(), local.position, local.rotation, local.scale);
    m_pose->setJointLocal(m_joint, local);
}

} // namespace Rapture

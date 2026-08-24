#ifndef RAPTURE__BONE3D_H
#define RAPTURE__BONE3D_H

#include "assets/skeletons/Skeleton.h"
#include "scene/instances/Node3D.h"

namespace Rapture {

class SkeletonPose;

/**
 * @brief One joint of a skeleton, placed by whoever poses it and read by whatever follows it.
 */
class Bone3D : public Node3D {
  public:
    Bone3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief Moves this bone to where the pose already has it, without telling it again
     * @param transform The local transform the pose puts this bone's joint at
     */
    void setPosedLocalTransform(const glm::mat4 &transform);

    Skeleton::JointIndex getJoint() const { return m_joint; }

    /**
     * @brief Binds this bone to the joint it stands for
     * @param pose The pose holding that joint
     * @param joint The joint
     */
    void bindJoint(SkeletonPose *pose, Skeleton::JointIndex joint);

  protected:
    void onLocalTransformChanged() override;

  private:
    SkeletonPose *m_pose = nullptr;
    Skeleton::JointIndex m_joint = Skeleton::INVALID_JOINT_INDEX;
};

} // namespace Rapture

#endif // RAPTURE__BONE3D_H

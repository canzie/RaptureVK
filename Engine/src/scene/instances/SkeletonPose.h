#ifndef RAPTURE__SKELETON_POSE_H
#define RAPTURE__SKELETON_POSE_H

#include "assets/asset_manager/AssetCommon.h"
#include "assets/skeletons/Skeleton.h"
#include "scene/instances/Node3D.h"
#include "scene/render_data/SkeletonInstanceManager.h"

#include <glm/glm.hpp>

#include <memory>
#include <span>
#include <vector>

namespace Rapture {

class Bone3D;

/**
 * @brief A skeleton being posed, which any number of skeletal meshes are drawn against.
 */
class SkeletonPose : public Node3D {
  public:
    SkeletonPose(Scene &scene, std::string_view name, AssetHandle skeleton);
    SkeletonPose(Scene &scene, std::string_view name);
    ~SkeletonPose() override;

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    AssetHandle skeleton() const { return m_skeleton; }

    /**
     * @brief The bones this pose is drawn from
     * @return The bones, or nullptr if the skeleton could not be resolved
     */
    SkeletonInstance *skeletonInstance() const { return m_skeletonInstance.get(); }

    uint32_t getJointCount() const { return static_cast<uint32_t>(m_localPose.size()); }

    /**
     * @brief The joint each joint hangs from
     * @return One parent per joint, in joint order
     */
    std::span<const Skeleton::JointIndex> getJointParents() const;

    /**
     * @brief Where a joint sits relative to the joint above it
     * @param joint The joint
     * @return Its local transform
     */
    const Skeleton::JointTransform &getJointLocal(Skeleton::JointIndex joint) const { return m_localPose[joint]; }

    /**
     * @brief Moves a joint relative to the joint above it
     * @param joint The joint to move
     * @param transform Where it should sit
     */
    void setJointLocal(Skeleton::JointIndex joint, const Skeleton::JointTransform &transform);

    /**
     * @brief The bone standing for a joint
     * @param joint The joint
     * @return Its bone
     */
    Bone3D *getBone(Skeleton::JointIndex joint) const { return m_bones[joint]; }

    /**
     * @brief Puts every joint back where the skeleton rests it
     */
    void resetToRestPose();

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  private:
    /**
     * @brief Resolves the skeleton and takes room for its bones, dropping any skeleton already posed
     * @param skeleton The skeleton to pose
     */
    void bindSkeleton(AssetHandle skeleton);

    /**
     * @brief Replaces the bones with one per joint of the bound skeleton
     */
    void buildBones();

    /**
     * @brief Places every bone where the local pose puts its joint
     */
    void pushPoseToBones();

    /**
     * @brief Puts this pose's joints where the draws deformed by it read them
     */
    void writeBoneMatrices();

  private:
    AssetHandle m_skeleton = INVALID_ASSET_HANDLE;
    std::unique_ptr<SkeletonInstance> m_skeletonInstance;
    std::vector<Skeleton::JointTransform> m_localPose;
    std::vector<Bone3D *> m_bones;
    std::vector<glm::mat4> m_boneMatrices;
};

} // namespace Rapture

#endif // RAPTURE__SKELETON_POSE_H

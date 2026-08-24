#ifndef RAPTURE__SKELETON_GIZMO_H
#define RAPTURE__SKELETON_GIZMO_H

#include "layers/workspaces/Workspace.h"

#include <assets/skeletons/Skeleton.h>
#include <renderer/GizmoDrawList.h>

#include <vector>

namespace Rapture {
class SkeletonPose;
} // namespace Rapture

namespace gizmo {

/**
 * @brief The bones and joints of posed skeletons, drawn so they can be seen and picked
 *
 * The mode decides how finely what is drawn can be picked apart: a whole skeleton answers as one
 * object, a posed one answers per bone, and an edited one answers for its joints as well.
 */
class SkeletonGizmo {
  public:
    explicit SkeletonGizmo(EditorMode mode);

    void setMode(EditorMode mode) { m_mode = mode; }

    /**
     * @brief Sets whether the bones sit over the scene or are occluded by it
     * @param inFront True to draw them over whatever is in the way
     */
    void setDrawInFront(bool inFront) { m_depthMode = inFront ? Rapture::DEPTH_MODE_ALWAYS_IN_FRONT : Rapture::DEPTH_MODE_TESTED; }

    /**
     * @brief Adds one joint and the bones running from it to the joints below it
     * @param pose The pose to read the joint's place from
     * @param joint The joint to add
     */
    void addBone(const Rapture::SkeletonPose &pose, Rapture::Skeleton::JointIndex joint);

    /**
     * @brief Adds every joint of a pose and the bones between them
     * @param pose The pose to add
     */
    void addSkeleton(const Rapture::SkeletonPose &pose);

    /**
     * @brief Draws what has been added and empties this gizmo
     * @param drawList The list to draw into
     */
    void submit(Rapture::GizmoDrawList &drawList);

  private:
    void appendBones(const Rapture::SkeletonPose &pose, Rapture::Skeleton::JointIndex joint, Rapture::GizmoBatch &batch);
    void appendJoint(const Rapture::SkeletonPose &pose, Rapture::Skeleton::JointIndex joint, Rapture::GizmoBatch &batch);

    /**
     * @brief Takes the next batch of what this gizmo holds
     * @param userData What a query over the batch's gizmos reports
     * @return The batch, holding nothing
     */
    Rapture::GizmoBatch &nextBatch(uint64_t userData);

  private:
    std::vector<Rapture::GizmoBatch> m_batches;
    uint32_t m_usedBatches = 0;
    EditorMode m_mode;
    Rapture::DepthMode m_depthMode = Rapture::DEPTH_MODE_ALWAYS_IN_FRONT;
};

} // namespace gizmo

#endif // RAPTURE__SKELETON_GIZMO_H

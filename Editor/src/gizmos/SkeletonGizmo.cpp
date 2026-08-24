#include "SkeletonGizmo.h"

#include <scene/instances/Bone3D.h>
#include <scene/instances/SkeletonPose.h>

#include <cmath>
#include <span>

namespace gizmo {

static const glm::vec4 COL_SKELETON{0.784f, 0.643f, 0.310f, 1.0f};

// A bone's body is widest a tenth of the way along it, and that width is a tenth of its length
static constexpr float BONE_SHOULDER = 0.1f;
static constexpr float BONE_WIDTH = 0.1f;
static constexpr float JOINT_RADIUS = 0.02f;

/**
 * @brief An orthonormal pair spanning the plane an axis faces along
 * @param axis The axis, which must be normalized
 * @param tangent Receives the first axis of the plane
 * @param bitangent Receives the second axis of the plane
 */
static void s_buildPlaneBasis(const glm::vec3 &axis, glm::vec3 &tangent, glm::vec3 &bitangent)
{
    const glm::vec3 reference = std::abs(axis.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    tangent = glm::normalize(glm::cross(reference, axis));
    bitangent = glm::cross(axis, tangent);
}

static glm::vec3 s_jointPosition(const Rapture::SkeletonPose &pose, Rapture::Skeleton::JointIndex joint)
{
    return glm::vec3(pose.getBone(joint)->worldTransform()[3]);
}

SkeletonGizmo::SkeletonGizmo(EditorMode mode) : m_mode(mode) {}

void SkeletonGizmo::appendJoint(const Rapture::SkeletonPose &pose, Rapture::Skeleton::JointIndex joint, Rapture::GizmoBatch &batch)
{
    const glm::vec3 position = s_jointPosition(pose, joint);
    batch.sphereFilled(position, JOINT_RADIUS, COL_SKELETON);
}

void SkeletonGizmo::appendBones(const Rapture::SkeletonPose &pose, Rapture::Skeleton::JointIndex joint, Rapture::GizmoBatch &batch)
{
    const std::span<const Rapture::Skeleton::JointIndex> parents = pose.getJointParents();
    const glm::vec3 head = s_jointPosition(pose, joint);

    for (Rapture::Skeleton::JointIndex child = joint + 1; child < parents.size(); ++child) {
        if (parents[child] != joint) {
            continue;
        }

        const glm::vec3 tail = s_jointPosition(pose, child);
        const glm::vec3 along = tail - head;
        const float length = glm::length(along);
        if (length <= 0.0f) {
            continue;
        }

        const glm::vec3 axis = along / length;
        glm::vec3 tangent;
        glm::vec3 bitangent;
        s_buildPlaneBasis(axis, tangent, bitangent);

        const float radius = length * BONE_WIDTH;
        const glm::vec3 shoulder = head + axis * (length * BONE_SHOULDER);

        const glm::vec3 ring[4] = {shoulder + tangent * radius, shoulder + bitangent * radius, shoulder - tangent * radius,
                                   shoulder - bitangent * radius};

        for (uint32_t i = 0; i < 4; ++i) {
            const glm::vec3 &a = ring[i];
            const glm::vec3 &b = ring[(i + 1) % 4];

            batch.triangleFilled(head, a, b, COL_SKELETON);
            batch.triangleFilled(tail, b, a, COL_SKELETON);
        }
    }
}

Rapture::GizmoBatch &SkeletonGizmo::nextBatch(uint64_t userData)
{
    if (m_usedBatches == m_batches.size()) {
        m_batches.emplace_back(m_depthMode, Rapture::GIZMO_SHADING_MODE_SHADED, userData);
        return m_batches[m_usedBatches++];
    }

    Rapture::GizmoBatch &batch = m_batches[m_usedBatches++];
    batch.reset();
    batch.setDepthMode(m_depthMode);
    batch.setUserData(userData);

    return batch;
}

void SkeletonGizmo::addBone(const Rapture::SkeletonPose &pose, Rapture::Skeleton::JointIndex joint)
{
    const Rapture::ecs::Entity entity = pose.getBone(joint)->entity();

    if (m_mode == EDITOR_MODE_EDIT) {
        // an edited joint is grabbed on its own, the way its bones are
        appendJoint(pose, joint, nextBatch(entity));
        appendBones(pose, joint, nextBatch(entity));
        return;
    }

    Rapture::GizmoBatch &bones = nextBatch(entity);
    appendJoint(pose, joint, bones);
    appendBones(pose, joint, bones);
}

void SkeletonGizmo::addSkeleton(const Rapture::SkeletonPose &pose)
{
    const uint32_t jointCount = pose.getJointCount();

    if (m_mode == EDITOR_MODE_OBJECT) {
        Rapture::GizmoBatch &batch = nextBatch(pose.entity());

        for (Rapture::Skeleton::JointIndex joint = 0; joint < jointCount; ++joint) {
            appendBones(pose, joint, batch);
            appendJoint(pose, joint, batch);
        }

        return;
    }

    for (Rapture::Skeleton::JointIndex joint = 0; joint < jointCount; ++joint) {
        addBone(pose, joint);
    }
}

void SkeletonGizmo::submit(Rapture::GizmoDrawList &drawList)
{
    drawList.submit(std::span(m_batches).first(m_usedBatches));
    m_usedBatches = 0;
}

} // namespace gizmo

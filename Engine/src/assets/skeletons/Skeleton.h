#ifndef RAPTURE__SKELETON_H
#define RAPTURE__SKELETON_H

#include "core/events/EventSignal.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Rapture {

/**
 * @brief The joint hierarchy and bind pose a skinned mesh is posed against
 *
 * Joints are ordered so that a joint always follows its parent.
 */
class Skeleton {
  public:
    using JointIndex = uint32_t;

    static constexpr JointIndex INVALID_JOINT_INDEX = UINT32_MAX;

    struct JointTransform {
        glm::vec3 position = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
    };

    /**
     * @brief One local transform per joint, in joint order
     */
    struct Pose {
        std::vector<JointTransform> joints;
    };

    Skeleton() = default;
    Skeleton(std::vector<JointIndex> parents, std::vector<std::string> names, Pose restPose);

    /**
     * @brief Append a joint below an existing one
     * @param name Name for the new joint
     * @param parent Joint the new one hangs from, or INVALID_JOINT_INDEX for a root
     * @param restTransform The new joint's local transform when nothing drives it
     * @return Index of the new joint, or INVALID_JOINT_INDEX if parent names no joint
     */
    JointIndex addJoint(std::string name, JointIndex parent, const JointTransform &restTransform);

    /**
     * @brief Remove a joint, leaving its children hanging from its parent
     * @param joint Joint to remove
     * @return True if the joint was removed
     */
    bool removeJoint(JointIndex joint);

    /**
     * @brief Hang a joint from a different one
     * @param joint Joint to move
     * @param parent Joint it should hang from, which must precede it, or INVALID_JOINT_INDEX for a root
     * @return True if the joint was moved
     */
    bool setParent(JointIndex joint, JointIndex parent);

    void setName(JointIndex joint, std::string name);
    void setRestTransform(JointIndex joint, const JointTransform &restTransform);

    /**
     * @brief Serializes this skeleton into a self contained blob
     * @return The serialized bytes
     */
    std::vector<uint8_t> serialize() const;

    /**
     * @brief Builds a skeleton from a blob produced by serialize
     * @param blob The serialized skeleton bytes
     * @return The skeleton, or nullptr if the blob is invalid
     */
    static std::unique_ptr<Skeleton> deserialize(std::span<const uint8_t> blob);

    uint32_t getJointCount() const { return static_cast<uint32_t>(m_parents.size()); }

    /**
     * @brief The joint a joint hangs from
     * @param joint Index of the joint
     * @return Index of its parent, or INVALID_JOINT_INDEX if the joint is a root
     */
    JointIndex getParent(JointIndex joint) const { return m_parents[joint]; }

    /**
     * @brief Find a joint by name
     * @param name Name to look for
     * @return Index of the joint, or INVALID_JOINT_INDEX if no joint has that name
     */
    JointIndex findJoint(std::string_view name) const;

    const std::vector<JointIndex> &getParents() const { return m_parents; }
    const std::vector<std::string> &getNames() const { return m_names; }
    const Pose &getRestPose() const { return m_restPose; }

  public:
    /**
     * @brief Fires after a joint is added, removed or edited
     */
    EventSignal<void()> onChanged;

  private:
    std::vector<JointIndex> m_parents;
    std::vector<std::string> m_names;
    Pose m_restPose;
};

} // namespace Rapture

#endif // RAPTURE__SKELETON_H

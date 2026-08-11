#ifndef RAPTURE__NODE3D_H
#define RAPTURE__NODE3D_H

#include "scenes/instances/SceneObject.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rapture {

/**
 * @brief Which of a node's local transform representations is behind the one that was last written.
 *
 * Position is left out because a matrix carries it in its fourth column, so it is never derived.
 * The two are opposite directions of the same reconciliation and are never both set, so whichever
 * is clear is the representation that currently holds the truth. World transforms are not listed
 * because they are rebuilt as soon as anything moves, so they are never behind.
 */
enum TransformDirtyFlag {
    TRANSFORM_DIRTY_NONE = 0,
    TRANSFORM_DIRTY_LOCAL = 1,
    TRANSFORM_DIRTY_ROTATION_AND_SCALE = 2
};

/**
 * @brief An instance with a place in the world.
 */
class Node3D : public SceneObject {
  public:
    Node3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    glm::vec3 position() const;
    void setPosition(const glm::vec3 &position);

    const glm::vec3 &rotation() const;
    void setRotation(const glm::vec3 &rotation);

    const glm::quat &rotationQuat() const;
    void setRotation(const glm::quat &rotation);

    const glm::vec3 &scale() const;
    void setScale(const glm::vec3 &scale);

    /**
     * @brief This node's transform relative to the node above it
     * @return The local transform, rebuilt from rotation and scale if they were written last
     */
    const glm::mat4 &localTransform() const;

    /**
     * @brief Replaces this node's local transform, leaving rotation and scale to be read back out of it
     * @param transform The local transform
     */
    void setLocalTransform(const glm::mat4 &transform);

    /**
     * @brief The local transform composed with every Node3D above it
     * @return The world transform
     */
    const glm::mat4 &worldTransform() const;

    /**
     * @brief Rebuilds this node's world transform and every world transform below it
     */
    void updateWorldTransform();

    /**
     * @brief Places this node in the world, converting through the node above it
     * @param transform The world transform this node should end up with
     */
    void setWorldTransform(const glm::mat4 &transform);

    /**
     * @brief Takes a world transform the simulation produced, without queueing it back into physics
     * @param transform The world transform the body ended up at
     */
    void setSimulatedWorldTransform(const glm::mat4 &transform);

    /**
     * @brief The closest node above this one that has a place in the world
     * @return The ancestor, or nullptr if this node's transform is already a world transform
     */
    Node3D *parentNode() const;

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  protected:
    void onParentChanged() override;

  private:
    /**
     * @brief Records that rotation or scale was written, staling the local matrix
     */
    void markRotationAndScaleWritten();

    /**
     * @brief Records that the local matrix was written, staling rotation and scale
     */
    void markLocalWritten();

    /**
     * @brief Rebuilds every world transform below an instance, walking through instances that have none
     * @param parent The instance whose subtree is rebuilt
     */
    static void updateDescendantWorldTransforms(const SceneObject &parent);

    void resolveRotationAndScale() const;
    void resolveLocal() const;

  private:
    mutable glm::quat m_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    mutable glm::vec3 m_eulerRotation{0.0f};
    mutable glm::vec3 m_scale{1.0f};
    mutable uint32_t m_dirtyMask = TRANSFORM_DIRTY_NONE;
};

} // namespace Rapture

#endif // RAPTURE__NODE3D_H

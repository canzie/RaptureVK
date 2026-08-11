#ifndef RAPTURE__RIGID_BODY3D_H
#define RAPTURE__RIGID_BODY3D_H

#include "physics/Common.h"
#include "scenes/instances/SceneComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rapture {

class Node3D;

/**
 * @brief A body in the rigid body simulation, driving the transform of the object it is part of.
 *
 * The body sits at its object's world transform composed with its own local transform.
 */
class RigidBody3D : public SceneComponent {
  public:
    RigidBody3D(Scene &scene, std::string_view name);
    ~RigidBody3D() override;

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief Hands a simulated transform to the object this body drives
     * @param position Where the simulation put the body
     * @param rotation How the simulation oriented the body
     */
    void applySimulatedTransform(const glm::vec3 &position, const glm::quat &rotation);

    /**
     * @brief Recreates the body from the current settings, at the transform its object sits at now
     */
    void rebuild();

    const PhysicsShape &shape() const { return m_shape; }
    void setShape(const PhysicsShape &shape);

    /**
     * @brief This body's transform relative to the object it is part of
     */
    const glm::mat4 &localTransform() const { return m_localTransform; }
    void setLocalTransform(const glm::mat4 &transform);

    PhysicsMotionType motionType() const { return m_motionType; }
    void setMotionType(PhysicsMotionType motionType);

    float friction() const { return m_friction; }
    void setFriction(float friction);

    float restitution() const { return m_restitution; }
    void setRestitution(float restitution);

    /**
     * @brief The object this body moves
     * @return The object, or nullptr if it is detached or has no place in the world
     */
    Node3D *node() const;

    PhysicsBodyId bodyId() const { return m_bodyId; }

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  protected:
    void onAttach() override;
    void onDetach() override;

  private:
    /**
     * @brief This body's local transform with its translation taken into world units
     * @return The scaled local transform, without scale of its own
     */
    glm::mat4 scaledLocalTransform() const;

    /**
     * @brief Where the body itself sits in the world
     * @param local The transform from scaledLocalTransform
     * @return The body's world transform, without scale
     */
    glm::mat4 bodyTransform(const glm::mat4 &local) const;

    /**
     * @brief Takes the body out of the simulation, if it is in one
     */
    void releaseBody();

  private:
    PhysicsShape m_shape = PhysicsBoxShape{};
    glm::mat4 m_localTransform{1.0f};
    PhysicsMotionType m_motionType = PHYSICS_MOTION_DYNAMIC;
    float m_friction = 0.2f;
    float m_restitution = 0.0f;
    bool m_startActive = true;

    PhysicsBodyId m_bodyId;
    glm::mat4 m_inverseLocal{1.0f};
};

} // namespace Rapture

#endif // RAPTURE__RIGID_BODY3D_H
